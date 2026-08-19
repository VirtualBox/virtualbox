/* $Id: VBoxMPDX.cpp 115080 2026-08-19 11:45:15Z vitali.pelenjow@oracle.com $ */
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


uint64_t SvgaGetGraphicsMemorySize(PVBOXMP_DEVEXT pDevExt)
{
    if (   pDevExt->pGa
        && pDevExt->pGa->hw.pSvga)
    {
        return pDevExt->pGa->hw.pSvga->u64GBObjectMemSize;
    }
    return 0;
}


DECLINLINE(DX_ALLOCATION_INSTANCE *) svgaGetAllocationInstance(PVBOXWDDM_ALLOCATION pAllocation, uint32_t OffsetInPages)
{
    DX_ALLOCATION_INSTANCE *pInstance = (DX_ALLOCATION_INSTANCE *)RTAvlU32Get(&pAllocation->dx.treeInstances, OffsetInPages);
    return pInstance;
}


static void svgaFreeAllocationInstance(PVBOXWDDM_ALLOCATION pAllocation, uint32_t OffsetInPages)
{
    DX_ALLOCATION_INSTANCE *pInstance = (DX_ALLOCATION_INSTANCE *)RTAvlU32Remove(&pAllocation->dx.treeInstances, OffsetInPages);
    AssertReturnVoid(pInstance);

    GaMemFree(pInstance);
}


static DX_ALLOCATION_INSTANCE *svgaAllocateAllocationInstance(PVBOXWDDM_ALLOCATION pAllocation, uint32_t OffsetInPages)
{
    Assert(svgaGetAllocationInstance(pAllocation, OffsetInPages) == NULL);

    DX_ALLOCATION_INSTANCE *pInstance = (DX_ALLOCATION_INSTANCE *)GaMemAllocZero(sizeof(DX_ALLOCATION_INSTANCE));
    AssertReturn(pInstance, NULL);

    pInstance->Core.Key = OffsetInPages;
    pInstance->mobid    = SVGA3D_INVALID_ID;

    bool fInserted = RTAvlU32Insert(&pAllocation->dx.treeInstances, &pInstance->Core);
    AssertReturnStmt(fInserted, GaMemFree(pInstance), NULL);

    return pInstance;
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
                    /* Such allocations have one instance. */
                    DX_ALLOCATION_INSTANCE *pInstance = svgaGetAllocationInstance(pAllocation, 0);
                    pCmd->mobid = pInstance ? pInstance->mobid : SVGA3D_INVALID_ID;
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


static DECLCALLBACK(int) svgaDestroyMobForAllocationCb(PAVLU32NODECORE pNode, void *pvUser)
{
    DX_ALLOCATION_INSTANCE *pInstance = (DX_ALLOCATION_INSTANCE *)pNode;
    VBOXWDDM_EXT_VMSVGA *pSvga = (VBOXWDDM_EXT_VMSVGA *)pvUser;

    if (pInstance->mobid != SVGA3D_INVALID_ID)
    {
        uint32_t cbRequired = 0;
        SvgaMobDestroy(pSvga, SVGA3D_INVALID_ID, NULL, 0, &cbRequired);
        void *pvCmd = SvgaCmdBufReserve(pSvga, cbRequired, SVGA3D_INVALID_ID);
        if (pvCmd)
        {
            SvgaMobDestroy(pSvga, pInstance->mobid, pvCmd, cbRequired, &cbRequired);
            SvgaCmdBufCommit(pSvga, cbRequired);
        }

        pInstance->mobid = SVGA3D_INVALID_ID;
    }

    return 0;
}


static void svgaDestroyMobForAllocation(VBOXWDDM_EXT_VMSVGA *pSvga, PVBOXWDDM_ALLOCATION pAllocation)
{
    RTAvlU32DoWithAll(&pAllocation->dx.treeInstances, 0, svgaDestroyMobForAllocationCb, pSvga);
}


static NTSTATUS svgaDefineMobForAllocation(VBOXWDDM_EXT_VMSVGA *pSvga, DX_ALLOCATION_INSTANCE *pInstance)
{

    /* Such allocations have one instance. Allocate a mobid. */
    NTSTATUS Status = SvgaMobAlloc(pSvga, &pInstance->mobid, pInstance->pGbo);
    if (NT_SUCCESS(Status))
    {
        /* Inform the host about the mob. */
        uint32_t cbCmd = 0;
        SvgaMobDefine(pSvga, SVGA3D_INVALID_ID, NULL, 0, &cbCmd);
        void *pvCmd = SvgaCmdBufReserve(pSvga, cbCmd, SVGA3D_INVALID_ID);
        if (pvCmd)
        {
            SvgaMobDefine(pSvga, pInstance->mobid, pvCmd, cbCmd, &cbCmd);
            SvgaCmdBufCommit(pSvga, cbCmd);
            return STATUS_SUCCESS;
        }

        AssertFailedStmt(Status = STATUS_INSUFFICIENT_RESOURCES);
        /* Deallocate mobid. */
        SvgaMobFree(pSvga, &pInstance->mobid);
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
    pAllocationInfo->MaximumRenamingListLength       = pAllocation->dx.SegmentId == 2
                                                     ? DX_MAX_RENAMING_LIST_LENGTH
                                                     : 1;
    pAllocationInfo->hAllocation                     = pAllocation;
    pAllocationInfo->pAllocationUsageHint            = NULL;
    pAllocationInfo->AllocationPriority              = D3DDDI_ALLOCATIONPRIORITY_NORMAL;

    /* Allocations in the host VRAM still need guest backing. */
    NTSTATUS Status;
    if (pAllocation->dx.SegmentId == 3 || pAllocation->dx.desc.fPrimary)
    {
        uint32_t const cbGB = RT_ALIGN_32(pAllocation->dx.desc.cbAllocation, PAGE_SIZE);

        /* Such allocations have one instance. */
        DX_ALLOCATION_INSTANCE *pInstance = svgaAllocateAllocationInstance(pAllocation, 0);
        if (pInstance)
        {
            Status = SvgaGboCreate(pSvga, &pInstance->pGbo, cbGB, "VMSVGAGB");
            if (NT_SUCCESS(Status))
            {
                Status = svgaDefineMobForAllocation(pSvga, pInstance);
                if (NT_SUCCESS(Status))
                {
                    Status = svgaCreateSurfaceForAllocation(pSvga, pAllocation);
                    if (!NT_SUCCESS(Status))
                        svgaDestroyMobForAllocation(pSvga, pAllocation);
                }

                if (!NT_SUCCESS(Status))
                    SvgaGboUnreference(pSvga, &pInstance->pGbo);
            }

            if (!NT_SUCCESS(Status))
                svgaFreeAllocationInstance(pAllocation, 0);
        }
        else
            Status = STATUS_INSUFFICIENT_RESOURCES;
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
    pAllocationInfo->MaximumRenamingListLength       = 1;
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
        if (pAllocation->dx.SegmentId == 3 || pAllocation->dx.desc.fPrimary)
        {
            /* Unbind mob from surface. */
            DX_ALLOCATION_INSTANCE *pInstance = svgaGetAllocationInstance(pAllocation, 0);
            if (pInstance && pInstance->mobid != SVGA3D_INVALID_ID)
            {
                /* Unbind mob */
                pvCmd = SvgaCmdBuf3dCmdReserve(pSvga, SVGA_3D_CMD_BIND_GB_SURFACE, sizeof(SVGA3dCmdBindGBSurface), SVGA3D_INVALID_ID);
                if (pvCmd)
                {
                    SVGA3dCmdBindGBSurface *pCmd = (SVGA3dCmdBindGBSurface *)pvCmd;
                    pCmd->sid = pAllocation->dx.sid;
                    pCmd->mobid = SVGA3D_INVALID_ID;
                    SvgaCmdBufCommit(pSvga, sizeof(SVGA3dCmdBindGBSurface));
                }
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
    //pAllocation->dx.SegmentId = 0;
    //pAllocation->dx.treeInstances = 0;

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


static DECLCALLBACK(int) svgaDestroyAllocationCb(PAVLU32NODECORE pNode, void *pvUser)
{
    DX_ALLOCATION_INSTANCE *pInstance = (DX_ALLOCATION_INSTANCE *)pNode;
    VBOXWDDM_EXT_VMSVGA *pSvga = (VBOXWDDM_EXT_VMSVGA *)pvUser;

    SvgaGboUnreference(pSvga, &pInstance->pGbo);
    GaMemFree(pInstance);
    return 0;
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

    RTAvlU32Destroy(&pAllocation->dx.treeInstances, svgaDestroyAllocationCb, pDevExt->pGa->hw.pSvga);

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


static uint32_t svgaAllocationInstanceMobFromOffset(PVBOXWDDM_ALLOCATION pAllocation, uint32_t OffsetInPages)
{
    DX_ALLOCATION_INSTANCE *pInstance = svgaGetAllocationInstance(pAllocation, OffsetInPages);
    AssertReturn(pInstance, SVGA3D_INVALID_ID);
    return pInstance->mobid;
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
            /* DriverId determines what the patch is about. */
            if (pIn->DriverId == VBOXDXPATCHID_SURFACE)
            {
                Assert(pAllocation->dx.desc.enmAllocationType == VBOXDXALLOCATIONTYPE_SURFACE);
                if (pAllocation->dx.sid != SVGA3D_INVALID_ID)
                    *(uint32_t *)pPatchAddress = pAllocation->dx.sid;

            }
            else if (   pIn->DriverId == VBOXDXPATCHID_SHADERS
                     || pIn->DriverId == VBOXDXPATCHID_CO)
            {
                Assert(   pAllocation->dx.desc.enmAllocationType == VBOXDXALLOCATIONTYPE_SHADERS
                       || pAllocation->dx.desc.enmAllocationType == VBOXDXALLOCATIONTYPE_CO);
                Assert(*(uint32_t *)pPatchAddress == SVGA3D_INVALID_ID);

                if (pAllocationListEntry->SegmentId)
                {
                    /* Allocation already paged in. */
                    AssertContinue(pAllocationListEntry->SegmentId == 2);

                    /* Such allocations have one instance. Still use generic code to get mobid from address. */
                    uint32_t const OffsetInPages = (uint32_t)(pAllocationListEntry->PhysicalAddress.QuadPart >> PAGE_SHIFT);
                    *(uint32_t *)pPatchAddress = svgaAllocationInstanceMobFromOffset(pAllocation, OffsetInPages);
                }
            }
            else if (pIn->DriverId == VBOXDXPATCHID_INSTANCEMOB)
            {
                Assert(*(uint32_t *)pPatchAddress == SVGA3D_INVALID_ID);

                if (pAllocationListEntry->SegmentId)
                {
                     AssertContinue(pAllocationListEntry->SegmentId == 2);

                     uint32_t const OffsetInPages = (uint32_t)(pAllocationListEntry->PhysicalAddress.QuadPart >> PAGE_SHIFT);
                     *(uint32_t *)pPatchAddress = svgaAllocationInstanceMobFromOffset(pAllocation, OffsetInPages);
                }
            }
        }
        else
        {
            if (   pIn->DriverId == VBOXDXPATCHID_SURFACE
                || pIn->DriverId == VBOXDXPATCHID_SHADERS
                || pIn->DriverId == VBOXDXPATCHID_CO)
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


NTSTATUS SvgaCommandFence(VBOXWDDM_EXT_VMSVGA *pSvga,
                          uint64_t u64FenceValue,
                          void *pvCmd,
                          uint32_t cbReserved,
                          uint32_t *pcbCmd)
{
    uint32_t cbRequired = sizeof(SVGA3dCmdHeader) + sizeof(SVGA3dCmdDXMobFence64);

    *pcbCmd += cbRequired;
    if (cbReserved < cbRequired)
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;

    /* Generate commands. */
    uint8_t *pu8Cmd = (uint8_t *)pvCmd;
    SVGA3dCmdHeader *pHdr;

    pHdr = (SVGA3dCmdHeader *)pu8Cmd;
    pHdr->id   = SVGA_3D_CMD_DX_MOB_FENCE_64;
    pHdr->size = sizeof(SVGA3dCmdDXMobFence64);
    pu8Cmd += sizeof(*pHdr);

    {
    SVGA3dCmdDXMobFence64 *pCmd = (SVGA3dCmdDXMobFence64 *)pu8Cmd;
    pCmd->value = u64FenceValue;
    pCmd->mobId = pSvga->mobidMiniport;
    pCmd->mobOffset = RT_OFFSETOF(VMSVGAMINIPORTMOB, u64MobFence);
    pu8Cmd += sizeof(*pCmd);
    }

    Assert((uintptr_t)pu8Cmd - (uintptr_t)pvCmd == cbRequired);

    return STATUS_SUCCESS;
}


DECLINLINE(int) SvgaFenceCmp64(uint64_t u64FenceA, uint64_t u64FenceB)
{
     if (   u64FenceA < u64FenceB
         || u64FenceA - u64FenceB > UINT64_MAX / 2)
         return -1; /* FenceA is newer than FenceB. */

     return u64FenceA > u64FenceB;
}


static NTSTATUS svgaPTHost2SysMem(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_ALLOCATION pAllocation,
                                  DXGKARG_BUILDPAGINGBUFFER *pBuildPagingBuffer, uint32_t *pcbCommands)
{
    GALOG(("PTHost2SysMem: MDL: offset %u, size %u, 0x%p/%u, stage %u\n",
           pBuildPagingBuffer->Transfer.Destination.pMdl->ByteOffset,
           pBuildPagingBuffer->Transfer.Destination.pMdl->ByteCount,
           pBuildPagingBuffer->Transfer.Destination.pMdl,
           pBuildPagingBuffer->Transfer.MdlOffset,
           pBuildPagingBuffer->MultipassOffset));

    /*
     * Steps that take partial transfers into account.
     * If MultipassOffset is 1, then wait for the MOB fence that was submitted for deleting of MOB.
     *  - if surface was not yet read back, that is mobid != SVGA3D_INVALID_ID:
     *    - readback surface;
     *    - unbind and delete current mob (pGbo still exists in the guest memory);
     *    - set mobid to SVGA3D_INVALID_ID;
     *    - set MultipassOffset to 1 and return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER.
     *
     *  - memcpy from dx.pGbo to the MDL.
     */

    VBOXWDDM_EXT_VMSVGA *pSvga = pDevExt->pGa->hw.pSvga;

    /* Segment 3 allocations have one instance. */
    DX_ALLOCATION_INSTANCE *pInstance = svgaGetAllocationInstance(pAllocation, 0);
    AssertReturn(pAllocation->dx.SegmentId == 3 && pInstance, STATUS_INVALID_PARAMETER);

    if (!pAllocation->dx.flags.fReadbackCompleted)
    {
        if (pBuildPagingBuffer->MultipassOffset == 0)
        {
            /* Start paging transfer for this surface by reading back its content. */
            /* MOB should be bound at this point. */
            Assert(pInstance->mobid != SVGA3D_INVALID_ID);

            /* Read back, unbind and destroy mob. */

            /*
             * How many bytes are required in the DMA buffer
             */
            uint32_t cbRequired = 0;
            SvgaMobDestroy(pSvga, SVGA3D_INVALID_ID, NULL, 0, &cbRequired);
            cbRequired += sizeof(SVGA3dCmdHeader) + sizeof(SVGA3dCmdReadbackGBSurface);
            SvgaCommandFence(pSvga, 0, NULL, 0, &cbRequired);
            cbRequired += sizeof(SVGA3dCmdHeader) + sizeof(SVGA3dCmdBindGBSurface);
            cbRequired += sizeof(SVGA3dCmdHeader) + sizeof(SVGA3dCmdInvalidateGBSurface);

            if (pBuildPagingBuffer->DmaSize < cbRequired)
                return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;

            uint8_t *pu8Cmd = (uint8_t *)pBuildPagingBuffer->pDmaBuffer;
            SVGA3dCmdHeader *pHdr;

            /*
             * Readback.
             */
            pHdr = (SVGA3dCmdHeader *)pu8Cmd;
            pHdr->id   = SVGA_3D_CMD_READBACK_GB_SURFACE;
            pHdr->size = sizeof(SVGA3dCmdReadbackGBSurface);
            pu8Cmd += sizeof(*pHdr);

            {
            SVGA3dCmdReadbackGBSurface *pCmd = (SVGA3dCmdReadbackGBSurface *)pu8Cmd;
            pCmd->sid         = pAllocation->dx.sid;
            pu8Cmd += sizeof(*pCmd);
            }

            /*
             * Fence that indicates that readback has been completed.
             */
            pAllocation->dx.u64LastReferencedCommandFence = ASMAtomicIncU64(&pSvga->u64MobFence);

            uint32_t cbCmd = 0;
            NTSTATUS Status = SvgaCommandFence(pSvga, pAllocation->dx.u64LastReferencedCommandFence, pu8Cmd,
                                               cbRequired - ((uintptr_t)pu8Cmd - (uintptr_t)pBuildPagingBuffer->pDmaBuffer),
                                               &cbCmd);
            AssertReturn(NT_SUCCESS(Status), Status);
            pu8Cmd += cbCmd;

            /*
             * Unbind the mob.
             */
            pHdr = (SVGA3dCmdHeader *)pu8Cmd;
            pHdr->id   = SVGA_3D_CMD_BIND_GB_SURFACE;
            pHdr->size = sizeof(SVGA3dCmdBindGBSurface);
            pu8Cmd += sizeof(*pHdr);

            {
            SVGA3dCmdBindGBSurface *pCmd = (SVGA3dCmdBindGBSurface *)pu8Cmd;
            pCmd->sid         = pAllocation->dx.sid;
            pCmd->mobid       = SVGA3D_INVALID_ID;
            pu8Cmd += sizeof(*pCmd);
            }

            /*
             * Destroy the mob.
             */
            cbCmd = 0;
            Status = SvgaMobDestroy(pSvga, pInstance->mobid, pu8Cmd,
                                    cbRequired - ((uintptr_t)pu8Cmd - (uintptr_t)pBuildPagingBuffer->pDmaBuffer),
                                    &cbCmd);
            AssertReturn(NT_SUCCESS(Status), Status);
            pu8Cmd += cbCmd;

            pHdr = (SVGA3dCmdHeader *)pu8Cmd;
            pHdr->id   = SVGA_3D_CMD_INVALIDATE_GB_SURFACE;
            pHdr->size = sizeof(SVGA3dCmdInvalidateGBSurface);
            pu8Cmd += sizeof(*pHdr);

            {
            SVGA3dCmdInvalidateGBSurface *pCmd = (SVGA3dCmdInvalidateGBSurface *)pu8Cmd;
            pCmd->sid = pAllocation->dx.sid;
            pu8Cmd += sizeof(*pCmd);
            }

            *pcbCommands = (uintptr_t)pu8Cmd - (uintptr_t)pBuildPagingBuffer->pDmaBuffer;
            Assert(*pcbCommands == cbRequired);

            pInstance->mobid = SVGA3D_INVALID_ID;

            /* Continue with the next stage. */
            pBuildPagingBuffer->MultipassOffset = 1;

            /* Return this to submit the current commands and get to the next stage of this transfer. */
            return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
        }

        if (pBuildPagingBuffer->MultipassOffset == 1)
        {
            /* Wait until submitted commands are processed by waiting for the fence. */
            uint64_t const u64CurrentCommandFence = ASMAtomicReadU64(&pSvga->pMiniportMobData->u64MobFence);
            if (SvgaFenceCmp64(pAllocation->dx.u64LastReferencedCommandFence, u64CurrentCommandFence) > 0)
                return STATUS_GRAPHICS_ALLOCATION_BUSY;

            pAllocation->dx.flags.fReadbackCompleted = true;
        }
    }

    /* Surface has been already read back to its GBO. */
    Assert(pAllocation->dx.flags.fReadbackCompleted);
    Assert(pInstance->mobid == SVGA3D_INVALID_ID);

    /* Copy data from GBO to MDL. */
    Assert(!pInstance->pGbo->flags.fMdl); /* The GBO must be allocated by the driver. */

    /* Check that transfer is within GBO memory. */
    AssertReturn(   pBuildPagingBuffer->Transfer.TransferOffset < pInstance->pGbo->cbGbo
                 && pBuildPagingBuffer->Transfer.TransferSize <= pInstance->pGbo->cbGbo - pBuildPagingBuffer->Transfer.TransferOffset,
                 STATUS_INVALID_PARAMETER);

    /* Src is the GBO. */
    uint8_t const *pu8Src = (uint8_t *)RTR0MemObjAddress(pInstance->pGbo->hMemObj);
    AssertReturn(pu8Src, STATUS_UNSUCCESSFUL);
    pu8Src += pBuildPagingBuffer->Transfer.TransferOffset;

    /* Dst is the MDL. */
    uint8_t *pu8Dst = (uint8_t *)MmGetSystemAddressForMdlSafe(
        pBuildPagingBuffer->Transfer.Destination.pMdl, NormalPagePriority);
    AssertReturn(pu8Dst, STATUS_UNSUCCESSFUL);
    pu8Dst += pBuildPagingBuffer->Transfer.MdlOffset * PAGE_SIZE;

    memcpy(pu8Dst, pu8Src, pBuildPagingBuffer->Transfer.TransferSize);

    /** @todo 'pInstance->pGbo' can be deallocated on 'pBuildPagingBuffer->Transfer.Flags.TransferEnd'
     * and reallocated in svgaPTSysMem2Host.
     */

    return STATUS_SUCCESS;
}


static NTSTATUS svgaPTSysMem2Host(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_ALLOCATION pAllocation,
                                  DXGKARG_BUILDPAGINGBUFFER *pBuildPagingBuffer, uint32_t *pcbCommands)
{
    GALOG(("PTSysMem2Host: MDL: offset %u, size %u, 0x%p/%u, stage %u\n",
           pBuildPagingBuffer->Transfer.Source.pMdl->ByteOffset,
           pBuildPagingBuffer->Transfer.Source.pMdl->ByteCount,
           pBuildPagingBuffer->Transfer.Source.pMdl,
           pBuildPagingBuffer->Transfer.MdlOffset,
           pBuildPagingBuffer->MultipassOffset));

    /* Copy data from MDL to the GBO until end of transfer (pBuildPagingBuffer->Transfer.Flags.TransferEnd).
     * Create a mob, bind the surface and update it on TransferEnd.
     */

    VBOXWDDM_EXT_VMSVGA *pSvga = pDevExt->pGa->hw.pSvga;

    /* Segment 3 allocations have one instance. */
    DX_ALLOCATION_INSTANCE *pInstance = svgaGetAllocationInstance(pAllocation, 0);
    AssertReturn(pAllocation->dx.SegmentId == 3 && pInstance, STATUS_INVALID_PARAMETER);

    Assert(pAllocation->dx.flags.fReadbackCompleted);
    Assert(pInstance->mobid == SVGA3D_INVALID_ID);

    /* Copy data from MDL to GBO. */
    Assert(!pInstance->pGbo->flags.fMdl); /* The GBO must be allocated by the driver. */

    /* Check that transfer is within GBO memory. */
    AssertReturn(   pBuildPagingBuffer->Transfer.TransferOffset < pInstance->pGbo->cbGbo
                 && pBuildPagingBuffer->Transfer.TransferSize <= pInstance->pGbo->cbGbo - pBuildPagingBuffer->Transfer.TransferOffset,
                 STATUS_INVALID_PARAMETER);

    /* Src is the MDL. */
    uint8_t const *pu8Src = (uint8_t *)MmGetSystemAddressForMdlSafe(
        pBuildPagingBuffer->Transfer.Source.pMdl, NormalPagePriority);
    AssertReturn(pu8Src, STATUS_UNSUCCESSFUL);
    pu8Src += pBuildPagingBuffer->Transfer.MdlOffset * PAGE_SIZE;

    /* Dst is the GBO. */
    uint8_t *pu8Dst = (uint8_t *)RTR0MemObjAddress(pInstance->pGbo->hMemObj);
    AssertReturn(pu8Dst, STATUS_UNSUCCESSFUL);
    pu8Dst += pBuildPagingBuffer->Transfer.TransferOffset;

    memcpy(pu8Dst, pu8Src, pBuildPagingBuffer->Transfer.TransferSize);

    if (pBuildPagingBuffer->Transfer.Flags.TransferEnd)
    {
        /*
         * Create a mob, bind the surface and update it.
         */
        uint32_t cbRequired = 0;
        SvgaMobDefine(pSvga, SVGA3D_INVALID_ID, NULL, 0, &cbRequired);
        cbRequired += sizeof(SVGA3dCmdHeader) + sizeof(SVGA3dCmdBindGBSurface);
        cbRequired += sizeof(SVGA3dCmdHeader) + sizeof(SVGA3dCmdUpdateGBSurface);

        if (pBuildPagingBuffer->DmaSize < cbRequired)
            return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;

        uint8_t *pu8Cmd = (uint8_t *)pBuildPagingBuffer->pDmaBuffer;
        SVGA3dCmdHeader *pHdr;

        /* Create a new mob */
        /// @todo svgaDefineMobForAllocation?
        NTSTATUS Status = SvgaMobAlloc(pSvga, &pInstance->mobid, pInstance->pGbo);
        AssertReturn(NT_SUCCESS(Status), Status);

        uint32_t cbCmd = 0;
        Status = SvgaMobDefine(pSvga, pInstance->mobid, pu8Cmd,
                               cbRequired - ((uintptr_t)pu8Cmd - (uintptr_t)pBuildPagingBuffer->pDmaBuffer),
                               &cbCmd);
        AssertReturnStmt(NT_SUCCESS(Status), SvgaMobFree(pSvga, &pInstance->mobid), Status);
        pu8Cmd += cbCmd;

        /* Bind the surface */
        pHdr = (SVGA3dCmdHeader *)pu8Cmd;
        pHdr->id   = SVGA_3D_CMD_BIND_GB_SURFACE;
        pHdr->size = sizeof(SVGA3dCmdBindGBSurface);
        pu8Cmd += sizeof(*pHdr);

        {
        SVGA3dCmdBindGBSurface *pCmd = (SVGA3dCmdBindGBSurface *)pu8Cmd;
        pCmd->sid         = pAllocation->dx.sid;
        pCmd->mobid       = pInstance->mobid;
        pu8Cmd += sizeof(*pCmd);
        }

        /* Update the surface */
        pHdr = (SVGA3dCmdHeader *)pu8Cmd;
        pHdr->id   = SVGA_3D_CMD_UPDATE_GB_SURFACE;
        pHdr->size = sizeof(SVGA3dCmdUpdateGBSurface);
        pu8Cmd += sizeof(*pHdr);

        {
        SVGA3dCmdUpdateGBSurface *pCmd = (SVGA3dCmdUpdateGBSurface *)pu8Cmd;
        pCmd->sid = pAllocation->dx.sid;
        pu8Cmd += sizeof(*pCmd);
        }

        *pcbCommands = (uintptr_t)pu8Cmd - (uintptr_t)pBuildPagingBuffer->pDmaBuffer;
        Assert(*pcbCommands == cbRequired);

        pAllocation->dx.flags.fReadbackCompleted = false;
    }

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

    GALOG(("%u -> %u, offset %u, size %u, cbAllocation %u\n",
           pBuildPagingBuffer->Transfer.Source.SegmentId,
           pBuildPagingBuffer->Transfer.Destination.SegmentId,
           pBuildPagingBuffer->Transfer.TransferOffset,
           pBuildPagingBuffer->Transfer.TransferSize,
           cbAllocation));

    if (pBuildPagingBuffer->Transfer.TransferSize == 0)
        return STATUS_SUCCESS;

    if (!pBuildPagingBuffer->Transfer.Flags.AllocationIsIdle)
        return STATUS_GRAPHICS_ALLOCATION_BUSY;

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
            else if (pBuildPagingBuffer->Transfer.Destination.SegmentId == 3) /* To Host */
                Status = svgaPTSysMem2Host(pDevExt, pAllocation, pBuildPagingBuffer, pcbCommands);
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

        case 3: /* From a host resource. */
        {
            if (pBuildPagingBuffer->Transfer.Destination.SegmentId == 0) /* To system memory */
                Status = svgaPTHost2SysMem(pDevExt, pAllocation, pBuildPagingBuffer, pcbCommands);
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
                /* Such allocations have one instance. */
                DX_ALLOCATION_INSTANCE *pInstance = svgaGetAllocationInstance(pAllocation, 0);
                AssertReturn(pInstance && pInstance->pGbo && pInstance->pGbo->hMemObj != NIL_RTR0MEMOBJ, STATUS_INVALID_PARAMETER);
                pvDst = RTR0MemObjAddress(pInstance->pGbo->hMemObj);
            }
            else
            {
                /* This driver does not expect to get Fill requests for other types of allocations. */
                DEBUG_BREAKPOINT_TEST();
                break;
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
    AssertReturn(pBuildPagingBuffer->MapApertureSegment.OffsetInPages <= UINT32_MAX, STATUS_INVALID_PARAMETER);

    /** @todo Mobs require locked pages. Could DX provide a Mdl without locked pages? */
    Assert(pBuildPagingBuffer->MapApertureSegment.pMdl->MdlFlags & MDL_PAGES_LOCKED);

    uint32_t cbRequired = 0;
    SvgaMobDefine(pSvga, SVGA3D_INVALID_ID, NULL, 0, &cbRequired);
    if (pAllocation->dx.desc.enmAllocationType == VBOXDXALLOCATIONTYPE_SURFACE)
        cbRequired += sizeof(SVGA3dCmdHeader) + sizeof(SVGA3dCmdBindGBSurface);

    if (pBuildPagingBuffer->DmaSize < cbRequired)
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;

    DX_ALLOCATION_INSTANCE *pInstance = svgaAllocateAllocationInstance(pAllocation, (uint32_t)pBuildPagingBuffer->MapApertureSegment.OffsetInPages);
    AssertReturn(pInstance, STATUS_INSUFFICIENT_RESOURCES);

    Assert(pInstance->pGbo == NULL && pInstance->mobid == SVGA3D_INVALID_ID);

    NTSTATUS Status = SvgaGboCreateForMdl(pSvga, &pInstance->pGbo,
                                          pBuildPagingBuffer->MapApertureSegment.NumberOfPages,
                                          pBuildPagingBuffer->MapApertureSegment.pMdl,
                                          pBuildPagingBuffer->MapApertureSegment.MdlOffset);
    AssertReturn(NT_SUCCESS(Status), Status);

    Status = SvgaMobAlloc(pSvga, &pInstance->mobid, pInstance->pGbo);
    AssertReturnStmt(NT_SUCCESS(Status), SvgaGboUnreference(pSvga, &pInstance->pGbo), Status);

    uint8_t *pu8Cmd = (uint8_t *)pBuildPagingBuffer->pDmaBuffer;
    SVGA3dCmdHeader *pHdr;

    uint32_t cbCmd = 0;
    SvgaMobDefine(pSvga, pInstance->mobid, pu8Cmd,
                  cbRequired - ((uintptr_t)pu8Cmd - (uintptr_t)pBuildPagingBuffer->pDmaBuffer),
                  &cbCmd);
    pu8Cmd += cbCmd;

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
        pCmd->mobid       = pInstance->mobid;
        pu8Cmd += sizeof(*pCmd);
        }
    }

    *pcbCommands = (uintptr_t)pu8Cmd - (uintptr_t)pBuildPagingBuffer->pDmaBuffer;
    Assert(*pcbCommands == cbRequired);

    return STATUS_SUCCESS;
}

static NTSTATUS svgaPagingUnmapApertureSegment(PVBOXMP_DEVEXT pDevExt, DXGKARG_BUILDPAGINGBUFFER *pBuildPagingBuffer, uint32_t *pcbCommands)
{
    VBOXWDDM_EXT_VMSVGA *pSvga = pDevExt->pGa->hw.pSvga;

    /* Unbind the allocation from the MOB and destroy the MOB which is bound to the allocation. */

    PVBOXWDDM_ALLOCATION pAllocation = (PVBOXWDDM_ALLOCATION)pBuildPagingBuffer->UnmapApertureSegment.hAllocation;
    AssertReturn(pAllocation, STATUS_INVALID_PARAMETER);
    AssertReturn(pBuildPagingBuffer->UnmapApertureSegment.SegmentId == 2, STATUS_INVALID_PARAMETER);
    AssertReturn(pBuildPagingBuffer->MapApertureSegment.OffsetInPages <= UINT32_MAX, STATUS_INVALID_PARAMETER);

    uint32_t cbRequired = 0;
    SvgaMobDestroy(pSvga, SVGA3D_INVALID_ID, NULL, 0, &cbRequired);

    if (pBuildPagingBuffer->DmaSize < cbRequired)
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;

    DX_ALLOCATION_INSTANCE *pInstance = svgaGetAllocationInstance(pAllocation, (uint32_t)pBuildPagingBuffer->MapApertureSegment.OffsetInPages);
    AssertReturn(pInstance, STATUS_INVALID_PARAMETER);

    uint8_t *pu8Cmd = (uint8_t *)pBuildPagingBuffer->pDmaBuffer;

    uint32_t cbCmd = 0;
    NTSTATUS Status = SvgaMobDestroy(pSvga, pInstance->mobid, pu8Cmd,
                                     cbRequired - ((uintptr_t)pu8Cmd - (uintptr_t)pBuildPagingBuffer->pDmaBuffer),
                                     &cbCmd);
    AssertReturn(NT_SUCCESS(Status), Status);
    pu8Cmd += cbCmd;

    SvgaGboUnreference(pSvga, &pInstance->pGbo);
    svgaFreeAllocationInstance(pAllocation, (uint32_t)pBuildPagingBuffer->MapApertureSegment.OffsetInPages);

    *pcbCommands = (uintptr_t)pu8Cmd - (uintptr_t)pBuildPagingBuffer->pDmaBuffer;
    Assert(*pcbCommands == cbRequired);

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

        /* Ignore a dummy patch request. */
        if (pPatchListEntry->PatchOffset == ~0UL)
            continue;

        AssertReturn(   pPatchListEntry->PatchOffset >= pPatch->DmaBufferSubmissionStartOffset
                     && pPatchListEntry->PatchOffset < pPatch->DmaBufferSubmissionEndOffset, STATUS_INVALID_PARAMETER);
        AssertReturn(pPatchListEntry->AllocationIndex < pPatch->AllocationListSize, STATUS_INVALID_PARAMETER);

        void * const pPatchAddress = (uint8_t *)pPatch->pDmaBuffer + pPatchListEntry->PatchOffset;

        DXGK_ALLOCATIONLIST const *pAllocationListEntry = &pPatch->pAllocationList[pPatchListEntry->AllocationIndex];
        AssertContinue(pAllocationListEntry->SegmentId != 0);

        PVBOXWDDM_OPENALLOCATION pOA = (PVBOXWDDM_OPENALLOCATION)pAllocationListEntry->hDeviceSpecificAllocation;
        if (pOA)
        {
            PVBOXWDDM_ALLOCATION pAllocation = pOA->pAllocation;
            /* DriverId determines what the patch is about. */
            if (pPatchListEntry->DriverId == VBOXDXPATCHID_SURFACE)
            {
                Assert(pAllocation->dx.desc.enmAllocationType == VBOXDXALLOCATIONTYPE_SURFACE);
                Assert(pAllocation->dx.sid != SVGA3D_INVALID_ID);
                *(uint32_t *)pPatchAddress = pAllocation->dx.sid;
            }
            else if (   pPatchListEntry->DriverId == VBOXDXPATCHID_SHADERS
                     || pPatchListEntry->DriverId == VBOXDXPATCHID_CO)
            {
                Assert(   pAllocation->dx.desc.enmAllocationType == VBOXDXALLOCATIONTYPE_SHADERS
                       || pAllocation->dx.desc.enmAllocationType == VBOXDXALLOCATIONTYPE_CO);
                AssertContinue(pAllocationListEntry->SegmentId == 2);

                if (*(uint32_t *)pPatchAddress == SVGA3D_INVALID_ID)
                {
                   /* Such allocations have one instance. Still use generic code to get mobid from address. */
                   uint32_t const OffsetInPages = (uint32_t)(pAllocationListEntry->PhysicalAddress.QuadPart >> PAGE_SHIFT);
                   *(uint32_t *)pPatchAddress = svgaAllocationInstanceMobFromOffset(pAllocation, OffsetInPages);
                }
            }
            else if (pPatchListEntry->DriverId == VBOXDXPATCHID_INSTANCEMOB)
            {
                AssertContinue(pAllocationListEntry->SegmentId == 2);

                if (*(uint32_t *)pPatchAddress == SVGA3D_INVALID_ID)
                {
                    uint32_t const OffsetInPages = (uint32_t)(pAllocationListEntry->PhysicalAddress.QuadPart >> PAGE_SHIFT);
                    *(uint32_t *)pPatchAddress = svgaAllocationInstanceMobFromOffset(pAllocation, OffsetInPages);
                }
            }
            else if (   pAllocation->enmType == VBOXWDDM_ALLOC_TYPE_STD_SHADOWSURFACE
                     || pAllocation->enmType == VBOXWDDM_ALLOC_TYPE_STD_STAGINGSURFACE
                     || pPatchListEntry->DriverId == VBOXDXPATCHID_VRAMOFFSET)
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
