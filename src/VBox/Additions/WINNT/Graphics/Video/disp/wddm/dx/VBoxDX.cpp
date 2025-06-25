/* $Id: VBoxDX.cpp 109972 2025-06-25 17:53:16Z vitali.pelenjow@oracle.com $ */
/** @file
 * VirtualBox D3D user mode driver.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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

#include <iprt/alloc.h>
#include <iprt/errcore.h>
#include <iprt/thread.h>
#include <VBox/log.h>

#include <iprt/win/windows.h>
#include <iprt/win/d3dkmthk.h>

#include <d3d10umddi.h>

#include "VBoxDX.h"
#include "VBoxDXCmd.h"

#pragma pack(1) /* VMSVGA structures are '__packed'. */
/* Declares a static array. */
#include <svga3d_surfacedefs.h>
#pragma pack()


static uint32_t vboxDXGetSubresourceOffset(PVBOXDX_RESOURCE pResource, UINT Subresource)
{
    surf_size_struct baseLevelSize;
    baseLevelSize.width  = pResource->AllocationDesc.surfaceInfo.size.width;
    baseLevelSize.height = pResource->AllocationDesc.surfaceInfo.size.height;
    baseLevelSize.depth  = pResource->AllocationDesc.surfaceInfo.size.depth;

    uint32_t const numMipLevels = pResource->AllocationDesc.surfaceInfo.numMipLevels;
    uint32_t const face = Subresource / numMipLevels;
    uint32_t const mip = Subresource % numMipLevels;
    return svga3dsurface_get_image_offset(pResource->AllocationDesc.surfaceInfo.format,
                                          baseLevelSize, numMipLevels, face, mip);
}


static uint32_t vboxDXGetSubresourceSize(PVBOXDX_RESOURCE pResource, UINT Subresource)
{
    surf_size_struct baseLevelSize;
    baseLevelSize.width  = pResource->AllocationDesc.surfaceInfo.size.width;
    baseLevelSize.height = pResource->AllocationDesc.surfaceInfo.size.height;
    baseLevelSize.depth  = pResource->AllocationDesc.surfaceInfo.size.depth;

    uint32_t const numMipLevels = pResource->AllocationDesc.surfaceInfo.numMipLevels;
    uint32_t const face = Subresource / numMipLevels;
    uint32_t const mip = Subresource % numMipLevels;

    const struct svga3d_surface_desc *desc = svga3dsurface_get_desc(pResource->AllocationDesc.surfaceInfo.format);

    surf_size_struct mipSize = svga3dsurface_get_mip_size(baseLevelSize, mip);
    return svga3dsurface_get_image_buffer_size(desc, &mipSize, 0);
}


static void vboxDXGetSubresourcePitch(PVBOXDX_RESOURCE pResource, UINT Subresource, UINT *pRowPitch, UINT *pDepthPitch)
{
    if (pResource->AllocationDesc.surfaceInfo.format == SVGA3D_BUFFER)
    {
        *pRowPitch = pResource->AllocationDesc.surfaceInfo.size.width;
        *pDepthPitch = *pRowPitch;
        return;
    }

    uint32_t const numMipLevels = pResource->AllocationDesc.surfaceInfo.numMipLevels;
    uint32_t const face = Subresource / numMipLevels;
    uint32_t const mip = Subresource % numMipLevels;

    surf_size_struct baseLevelSize;
    baseLevelSize.width  = pResource->AllocationDesc.surfaceInfo.size.width;
    baseLevelSize.height = pResource->AllocationDesc.surfaceInfo.size.height;
    baseLevelSize.depth  = pResource->AllocationDesc.surfaceInfo.size.depth;

    const struct svga3d_surface_desc *desc = svga3dsurface_get_desc(pResource->AllocationDesc.surfaceInfo.format);

    surf_size_struct mipSize = svga3dsurface_get_mip_size(baseLevelSize, mip);
    surf_size_struct blocks;
    svga3dsurface_get_size_in_blocks(desc, &mipSize, &blocks);

    *pRowPitch = blocks.width * desc->pitch_bytes_per_block;
    *pDepthPitch = blocks.height * (*pRowPitch);
}


static void vboxDXGetResourceBoxDimensions(PVBOXDX_RESOURCE pResource, UINT Subresource, SVGA3dBox *pBox,
                                           uint32_t *pOffPixel, uint32_t *pcbRow, uint32_t *pcRows, uint32_t *pDepth)
{
    if (pResource->AllocationDesc.surfaceInfo.format == SVGA3D_BUFFER)
    {
        *pOffPixel = pBox->x;
        *pcbRow = pBox->w;
        *pcRows = 1;
        *pDepth = 1;
        return;
    }

    const struct svga3d_surface_desc *desc = svga3dsurface_get_desc(pResource->AllocationDesc.surfaceInfo.format);

    surf_size_struct baseLevelSize;
    baseLevelSize.width  = pResource->AllocationDesc.surfaceInfo.size.width;
    baseLevelSize.height = pResource->AllocationDesc.surfaceInfo.size.height;
    baseLevelSize.depth  = pResource->AllocationDesc.surfaceInfo.size.depth;

    uint32_t const numMipLevels = pResource->AllocationDesc.surfaceInfo.numMipLevels;
    uint32_t const mip = Subresource % numMipLevels;

    surf_size_struct mipSize = svga3dsurface_get_mip_size(baseLevelSize, mip);

    surf_size_struct boxSize;
    boxSize.width = pBox->w;
    boxSize.height = pBox->h;
    boxSize.depth = pBox->d;
    surf_size_struct blocks;
    svga3dsurface_get_size_in_blocks(desc, &boxSize, &blocks);

    *pOffPixel = svga3dsurface_get_pixel_offset(pResource->AllocationDesc.surfaceInfo.format,
                                                mipSize.width, mipSize.height,
                                                pBox->x, pBox->y, pBox->z);
    *pcbRow = blocks.width * desc->pitch_bytes_per_block;
    *pcRows = blocks.height;
    *pDepth = pBox->d;
}

static void vboxDXGetSubresourceBox(PVBOXDX_RESOURCE pResource, UINT Subresource, SVGA3dBox *pBox)
{
    surf_size_struct baseLevelSize;
    baseLevelSize.width  = pResource->AllocationDesc.surfaceInfo.size.width;
    baseLevelSize.height = pResource->AllocationDesc.surfaceInfo.size.height;
    baseLevelSize.depth  = pResource->AllocationDesc.surfaceInfo.size.depth;

    uint32_t const numMipLevels = pResource->AllocationDesc.surfaceInfo.numMipLevels;
    uint32_t const mip = Subresource % numMipLevels;

    surf_size_struct mipSize = svga3dsurface_get_mip_size(baseLevelSize, mip);

    pBox->x = 0;
    pBox->y = 0;
    pBox->z = 0;
    pBox->w = mipSize.width;
    pBox->h = mipSize.height;
    pBox->d = mipSize.depth;
}


HRESULT vboxDXDeviceFlushCommands(PVBOXDX_DEVICE pDevice)
{
    LogFlowFunc(("pDevice %p, cbCommandBuffer %d\n", pDevice, pDevice->cbCommandBuffer));

    D3DDDICB_RENDER ddiRender;
    RT_ZERO(ddiRender);
    ddiRender.CommandLength     = pDevice->cbCommandBuffer;
    //ddiRender.CommandOffset     = 0;
    ddiRender.NumAllocations    = pDevice->cAllocations;
    ddiRender.NumPatchLocations = pDevice->cPatchLocations;
    //ddiRender.Flags             = 0;
    ddiRender.hContext          = pDevice->hContext;

    HRESULT hr = pDevice->pRTCallbacks->pfnRenderCb(pDevice->hRTDevice.handle, &ddiRender);
    AssertReturn(SUCCEEDED(hr), hr);

    pDevice->pCommandBuffer        = ddiRender.pNewCommandBuffer;
    pDevice->CommandBufferSize     = ddiRender.NewCommandBufferSize;
    pDevice->pAllocationList       = ddiRender.pNewAllocationList;
    pDevice->AllocationListSize    = ddiRender.NewAllocationListSize;
    pDevice->pPatchLocationList    = ddiRender.pNewPatchLocationList;
    pDevice->PatchLocationListSize = ddiRender.NewPatchLocationListSize;

    Assert(pDevice->cbCommandReserved == 0);
    pDevice->cbCommandBuffer = 0;
    pDevice->cAllocations    = 0;
    pDevice->cPatchLocations = 0;

    return S_OK;
}


void *vboxDXCommandBufferReserve(PVBOXDX_DEVICE pDevice, SVGAFifo3dCmdId enmCmd, uint32_t cbCmd, uint32_t cPatchLocations)
{
    Assert(pDevice->cbCommandBuffer <= pDevice->CommandBufferSize);

    uint32_t const cbReserve = sizeof(SVGA3dCmdHeader) + cbCmd;
    uint32_t cbAvail = pDevice->CommandBufferSize - pDevice->cbCommandBuffer;
    if (   cbAvail < cbReserve
        || pDevice->PatchLocationListSize - pDevice->cPatchLocations < cPatchLocations
        || pDevice->AllocationListSize - pDevice->cAllocations < cPatchLocations)
    {
        HRESULT hr = vboxDXDeviceFlushCommands(pDevice);
        if (FAILED(hr))
            return NULL;
        cbAvail = pDevice->CommandBufferSize - pDevice->cbCommandBuffer;
        AssertReturn(cbAvail >= cbReserve, NULL);
    }

    pDevice->cbCommandReserved = cbReserve;

    SVGA3dCmdHeader *pHeader = (SVGA3dCmdHeader *)((uint8_t *)pDevice->pCommandBuffer + pDevice->cbCommandBuffer);
    pHeader->id = enmCmd;
    pHeader->size = cbCmd;
    return &pHeader[1];
}


void vboxDXCommandBufferCommit(PVBOXDX_DEVICE pDevice)
{
    Assert(pDevice->cbCommandBuffer <= pDevice->CommandBufferSize);
    Assert(pDevice->cbCommandReserved <= pDevice->CommandBufferSize - pDevice->cbCommandBuffer);
    pDevice->cbCommandBuffer += pDevice->cbCommandReserved;
    pDevice->cbCommandReserved = 0;
}


void vboxDXStorePatchLocation(PVBOXDX_DEVICE pDevice, void *pvPatch, VBOXDXALLOCATIONTYPE enmAllocationType,
                              D3DKMT_HANDLE hAllocation, uint32_t offAllocation, bool fWriteOperation)
{
    if (!hAllocation)
        return;

    /* Find the same hAllocation */
    int idxAllocation = -1;
    for (unsigned i = 0; i < pDevice->cAllocations; ++i)
    {
         D3DDDI_ALLOCATIONLIST *p = &pDevice->pAllocationList[i];
         if (p->hAllocation == hAllocation)
         {
             idxAllocation = i;
             break;
         }
    }

    /* If allocation is already in the list, then do not touch its WriteOperation flag.
     * Trying to do 'pAllocationEntry->WriteOperation |= fWriteOperation' caused
     * problems when opening Windows 10 start menu and when switching between Windows 8
     * desktop and tile screens.
     */
    if (idxAllocation < 0)
    {
        /* Add allocation to the list. */
        idxAllocation = pDevice->cAllocations++;

        D3DDDI_ALLOCATIONLIST *pAllocationEntry = &pDevice->pAllocationList[idxAllocation];
        pAllocationEntry->hAllocation = hAllocation;
        pAllocationEntry->Value = 0;
        pAllocationEntry->WriteOperation = fWriteOperation;
    }

    D3DDDI_PATCHLOCATIONLIST *pPatchLocation = &pDevice->pPatchLocationList[pDevice->cPatchLocations];
    pPatchLocation->AllocationIndex = idxAllocation;
    pPatchLocation->Value = 0;
    pPatchLocation->DriverId = enmAllocationType;
    pPatchLocation->AllocationOffset = offAllocation;
    pPatchLocation->PatchOffset = (uintptr_t)pvPatch - (uintptr_t)pDevice->pCommandBuffer;
    pPatchLocation->SplitOffset = pDevice->cbCommandBuffer;
    ++pDevice->cPatchLocations;
}


static bool dxIsAllocationInUse(PVBOXDX_DEVICE pDevice, D3DKMT_HANDLE hAllocation)
{
    if (!hAllocation)
        return false;

    /* Find the same hAllocation */
    int idxAllocation = -1;
    for (unsigned i = 0; i < pDevice->cAllocations; ++i)
    {
         D3DDDI_ALLOCATIONLIST *p = &pDevice->pAllocationList[i];
         if (p->hAllocation == hAllocation)
         {
             idxAllocation = i;
             break;
         }
    }

    return idxAllocation >= 0;
}


static void vboxDXEmitSetConstantBuffers(PVBOXDX_DEVICE pDevice)
{
    for (unsigned idxShaderType = 0; idxShaderType < RT_ELEMENTS(pDevice->pipeline.aConstantBuffers); ++idxShaderType)
    {
        SVGA3dShaderType const enmShaderType = (SVGA3dShaderType)(idxShaderType + SVGA3D_SHADERTYPE_MIN);

        PVBOXDXCONSTANTBUFFERSSTATE pCBS = &pDevice->pipeline.aConstantBuffers[idxShaderType];
        for (unsigned i = pCBS->StartSlot; i < pCBS->StartSlot + pCBS->NumBuffers; ++i)
        {
            PVBOXDX_RESOURCE pResource = pCBS->apResource[i];
            if (pResource)
            {
                D3DKMT_HANDLE const hAllocation = vboxDXGetAllocation(pResource);
                uint32 const offsetInBytes = pCBS->aFirstConstant[i] * (4 * sizeof(UINT));
                uint32 const sizeInBytes = pCBS->aNumConstants[i] * (4 * sizeof(UINT));
                LogFunc(("type %d, slot %d, off %d, size %d, cbAllocation %d",
                         enmShaderType, i, offsetInBytes, sizeInBytes, pResource->AllocationDesc.cbAllocation));

                vgpu10SetSingleConstantBuffer(pDevice, i, enmShaderType, hAllocation, offsetInBytes, sizeInBytes);
            }
            else
                vgpu10SetSingleConstantBuffer(pDevice, i, enmShaderType, 0, 0, 0);
        }

        /* Trim empty slots. */
        while (pCBS->NumBuffers)
        {
            if (pCBS->apResource[pCBS->StartSlot + pCBS->NumBuffers - 1])
                break;
            --pCBS->NumBuffers;
        }

        while (pCBS->NumBuffers)
        {
            if (pCBS->apResource[pCBS->StartSlot])
                break;
            --pCBS->NumBuffers;
            ++pCBS->StartSlot;
        }
    }
}


static void vboxDXEmitSetVertexBuffers(PVBOXDX_DEVICE pDevice)
{
    PVBOXDXVERTEXBUFFERSSTATE pVBS = &pDevice->pipeline.VertexBuffers;

    /* Fetch allocation handles. */
    D3DKMT_HANDLE aAllocations[SVGA3D_MAX_VERTEX_ARRAYS];
    for (unsigned i = pVBS->StartSlot; i < pVBS->StartSlot + pVBS->NumBuffers; ++i)
    {
        PVBOXDX_RESOURCE pResource = pVBS->apResource[i];
        aAllocations[i] = vboxDXGetAllocation(pResource);
    }

    vgpu10SetVertexBuffers(pDevice, pVBS->StartSlot, pVBS->NumBuffers, aAllocations,
                           &pVBS->aStrides[pVBS->StartSlot], &pVBS->aOffsets[pVBS->StartSlot]);

    /* Trim empty slots. */
    while (pVBS->NumBuffers)
    {
        if (pVBS->apResource[pVBS->StartSlot + pVBS->NumBuffers - 1])
            break;
        --pVBS->NumBuffers;
    }

    while (pVBS->NumBuffers)
    {
        if (pVBS->apResource[pVBS->StartSlot])
            break;
        --pVBS->NumBuffers;
        ++pVBS->StartSlot;
    }
}


static void vboxDXEmitSetIndexBuffer(PVBOXDX_DEVICE pDevice)
{
    PVBOXDXINDEXBUFFERSTATE pIBS = &pDevice->pipeline.IndexBuffer;

    D3DKMT_HANDLE const hAllocation = vboxDXGetAllocation(pIBS->pBuffer);
    SVGA3dSurfaceFormat const svgaFormat = vboxDXDxgiToSvgaFormat(pIBS->Format);
    vgpu10SetIndexBuffer(pDevice, hAllocation, svgaFormat, pIBS->Offset);
}


static void vboxDXSetupPipeline(PVBOXDX_DEVICE pDevice)
{
    vboxDXEmitSetConstantBuffers(pDevice);
    vboxDXEmitSetVertexBuffers(pDevice);
    vboxDXEmitSetIndexBuffer(pDevice);
}


SVGA3dSurfaceFormat vboxDXDxgiToSvgaFormat(DXGI_FORMAT enmDxgiFormat)
{
    switch (enmDxgiFormat)
    {
        case DXGI_FORMAT_UNKNOWN:                    return SVGA3D_BUFFER;
        case DXGI_FORMAT_R32G32B32A32_TYPELESS:      return SVGA3D_R32G32B32A32_TYPELESS;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:         return SVGA3D_R32G32B32A32_FLOAT;
        case DXGI_FORMAT_R32G32B32A32_UINT:          return SVGA3D_R32G32B32A32_UINT;
        case DXGI_FORMAT_R32G32B32A32_SINT:          return SVGA3D_R32G32B32A32_SINT;
        case DXGI_FORMAT_R32G32B32_TYPELESS:         return SVGA3D_R32G32B32_TYPELESS;
        case DXGI_FORMAT_R32G32B32_FLOAT:            return SVGA3D_R32G32B32_FLOAT;
        case DXGI_FORMAT_R32G32B32_UINT:             return SVGA3D_R32G32B32_UINT;
        case DXGI_FORMAT_R32G32B32_SINT:             return SVGA3D_R32G32B32_SINT;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:      return SVGA3D_R16G16B16A16_TYPELESS;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:         return SVGA3D_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R16G16B16A16_UNORM:         return SVGA3D_R16G16B16A16_UNORM;
        case DXGI_FORMAT_R16G16B16A16_UINT:          return SVGA3D_R16G16B16A16_UINT;
        case DXGI_FORMAT_R16G16B16A16_SNORM:         return SVGA3D_R16G16B16A16_SNORM;
        case DXGI_FORMAT_R16G16B16A16_SINT:          return SVGA3D_R16G16B16A16_SINT;
        case DXGI_FORMAT_R32G32_TYPELESS:            return SVGA3D_R32G32_TYPELESS;
        case DXGI_FORMAT_R32G32_FLOAT:               return SVGA3D_R32G32_FLOAT;
        case DXGI_FORMAT_R32G32_UINT:                return SVGA3D_R32G32_UINT;
        case DXGI_FORMAT_R32G32_SINT:                return SVGA3D_R32G32_SINT;
        case DXGI_FORMAT_R32G8X24_TYPELESS:          return SVGA3D_R32G8X24_TYPELESS;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:       return SVGA3D_D32_FLOAT_S8X24_UINT;
        case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:   return SVGA3D_R32_FLOAT_X8X24;
        case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:    return SVGA3D_X32_G8X24_UINT;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:       return SVGA3D_R10G10B10A2_TYPELESS;
        case DXGI_FORMAT_R10G10B10A2_UNORM:          return SVGA3D_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R10G10B10A2_UINT:           return SVGA3D_R10G10B10A2_UINT;
        case DXGI_FORMAT_R11G11B10_FLOAT:            return SVGA3D_R11G11B10_FLOAT;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:          return SVGA3D_R8G8B8A8_TYPELESS;
        case DXGI_FORMAT_R8G8B8A8_UNORM:             return SVGA3D_R8G8B8A8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:        return SVGA3D_R8G8B8A8_UNORM_SRGB;
        case DXGI_FORMAT_R8G8B8A8_UINT:              return SVGA3D_R8G8B8A8_UINT;
        case DXGI_FORMAT_R8G8B8A8_SNORM:             return SVGA3D_R8G8B8A8_SNORM;
        case DXGI_FORMAT_R8G8B8A8_SINT:              return SVGA3D_R8G8B8A8_SINT;
        case DXGI_FORMAT_R16G16_TYPELESS:            return SVGA3D_R16G16_TYPELESS;
        case DXGI_FORMAT_R16G16_FLOAT:               return SVGA3D_R16G16_FLOAT;
        case DXGI_FORMAT_R16G16_UNORM:               return SVGA3D_R16G16_UNORM;
        case DXGI_FORMAT_R16G16_UINT:                return SVGA3D_R16G16_UINT;
        case DXGI_FORMAT_R16G16_SNORM:               return SVGA3D_R16G16_SNORM;
        case DXGI_FORMAT_R16G16_SINT:                return SVGA3D_R16G16_SINT;
        case DXGI_FORMAT_R32_TYPELESS:               return SVGA3D_R32_TYPELESS;
        case DXGI_FORMAT_D32_FLOAT:                  return SVGA3D_D32_FLOAT;
        case DXGI_FORMAT_R32_FLOAT:                  return SVGA3D_R32_FLOAT;
        case DXGI_FORMAT_R32_UINT:                   return SVGA3D_R32_UINT;
        case DXGI_FORMAT_R32_SINT:                   return SVGA3D_R32_SINT;
        case DXGI_FORMAT_R24G8_TYPELESS:             return SVGA3D_R24G8_TYPELESS;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:          return SVGA3D_D24_UNORM_S8_UINT;
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:      return SVGA3D_R24_UNORM_X8;
        case DXGI_FORMAT_X24_TYPELESS_G8_UINT:       return SVGA3D_X24_G8_UINT;
        case DXGI_FORMAT_R8G8_TYPELESS:              return SVGA3D_R8G8_TYPELESS;
        case DXGI_FORMAT_R8G8_UNORM:                 return SVGA3D_R8G8_UNORM;
        case DXGI_FORMAT_R8G8_UINT:                  return SVGA3D_R8G8_UINT;
        case DXGI_FORMAT_R8G8_SNORM:                 return SVGA3D_R8G8_SNORM;
        case DXGI_FORMAT_R8G8_SINT:                  return SVGA3D_R8G8_SINT;
        case DXGI_FORMAT_R16_TYPELESS:               return SVGA3D_R16_TYPELESS;
        case DXGI_FORMAT_R16_FLOAT:                  return SVGA3D_R16_FLOAT;
        case DXGI_FORMAT_D16_UNORM:                  return SVGA3D_D16_UNORM;
        case DXGI_FORMAT_R16_UNORM:                  return SVGA3D_R16_UNORM;
        case DXGI_FORMAT_R16_UINT:                   return SVGA3D_R16_UINT;
        case DXGI_FORMAT_R16_SNORM:                  return SVGA3D_R16_SNORM;
        case DXGI_FORMAT_R16_SINT:                   return SVGA3D_R16_SINT;
        case DXGI_FORMAT_R8_TYPELESS:                return SVGA3D_R8_TYPELESS;
        case DXGI_FORMAT_R8_UNORM:                   return SVGA3D_R8_UNORM;
        case DXGI_FORMAT_R8_UINT:                    return SVGA3D_R8_UINT;
        case DXGI_FORMAT_R8_SNORM:                   return SVGA3D_R8_SNORM;
        case DXGI_FORMAT_R8_SINT:                    return SVGA3D_R8_SINT;
        case DXGI_FORMAT_A8_UNORM:                   return SVGA3D_A8_UNORM;
        case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:         return SVGA3D_R9G9B9E5_SHAREDEXP;
        case DXGI_FORMAT_R8G8_B8G8_UNORM:            return SVGA3D_R8G8_B8G8_UNORM;
        case DXGI_FORMAT_G8R8_G8B8_UNORM:            return SVGA3D_G8R8_G8B8_UNORM;
        case DXGI_FORMAT_BC1_TYPELESS:               return SVGA3D_BC1_TYPELESS;
        case DXGI_FORMAT_BC1_UNORM:                  return SVGA3D_BC1_UNORM;
        case DXGI_FORMAT_BC1_UNORM_SRGB:             return SVGA3D_BC1_UNORM_SRGB;
        case DXGI_FORMAT_BC2_TYPELESS:               return SVGA3D_BC2_TYPELESS;
        case DXGI_FORMAT_BC2_UNORM:                  return SVGA3D_BC2_UNORM;
        case DXGI_FORMAT_BC2_UNORM_SRGB:             return SVGA3D_BC2_UNORM_SRGB;
        case DXGI_FORMAT_BC3_TYPELESS:               return SVGA3D_BC3_TYPELESS;
        case DXGI_FORMAT_BC3_UNORM:                  return SVGA3D_BC3_UNORM;
        case DXGI_FORMAT_BC3_UNORM_SRGB:             return SVGA3D_BC3_UNORM_SRGB;
        case DXGI_FORMAT_BC4_TYPELESS:               return SVGA3D_BC4_TYPELESS;
        case DXGI_FORMAT_BC4_UNORM:                  return SVGA3D_BC4_UNORM;
        case DXGI_FORMAT_BC4_SNORM:                  return SVGA3D_BC4_SNORM;
        case DXGI_FORMAT_BC5_TYPELESS:               return SVGA3D_BC5_TYPELESS;
        case DXGI_FORMAT_BC5_UNORM:                  return SVGA3D_BC5_UNORM;
        case DXGI_FORMAT_BC5_SNORM:                  return SVGA3D_BC5_SNORM;
        case DXGI_FORMAT_B5G6R5_UNORM:               return SVGA3D_B5G6R5_UNORM;
        case DXGI_FORMAT_B5G5R5A1_UNORM:             return SVGA3D_B5G5R5A1_UNORM;
        case DXGI_FORMAT_B8G8R8A8_UNORM:             return SVGA3D_B8G8R8A8_UNORM;
        case DXGI_FORMAT_B8G8R8X8_UNORM:             return SVGA3D_B8G8R8X8_UNORM;
        case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM: return SVGA3D_R10G10B10_XR_BIAS_A2_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:          return SVGA3D_B8G8R8A8_TYPELESS;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:        return SVGA3D_B8G8R8A8_UNORM_SRGB;
        case DXGI_FORMAT_B8G8R8X8_TYPELESS:          return SVGA3D_B8G8R8X8_TYPELESS;
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:        return SVGA3D_B8G8R8X8_UNORM_SRGB;
        case DXGI_FORMAT_BC6H_TYPELESS:              return SVGA3D_BC6H_TYPELESS;
        case DXGI_FORMAT_BC6H_UF16:                  return SVGA3D_BC6H_UF16;
        case DXGI_FORMAT_BC6H_SF16:                  return SVGA3D_BC6H_SF16;
        case DXGI_FORMAT_BC7_TYPELESS:               return SVGA3D_BC7_TYPELESS;
        case DXGI_FORMAT_BC7_UNORM:                  return SVGA3D_BC7_UNORM;
        case DXGI_FORMAT_BC7_UNORM_SRGB:             return SVGA3D_BC7_UNORM_SRGB;
        case DXGI_FORMAT_AYUV:                       return SVGA3D_AYUV;
        case DXGI_FORMAT_NV12:                       return SVGA3D_NV12;
        case DXGI_FORMAT_420_OPAQUE:                 return SVGA3D_NV12;
        case DXGI_FORMAT_YUY2:                       return SVGA3D_YUY2;
        case DXGI_FORMAT_P8:                         return SVGA3D_P8;
        case DXGI_FORMAT_B4G4R4A4_UNORM:             return SVGA3D_B4G4R4A4_UNORM;

        /* Does not seem to be a corresponding format for these: */
        case DXGI_FORMAT_R1_UNORM:
        case DXGI_FORMAT_Y410:
        case DXGI_FORMAT_Y416:
        case DXGI_FORMAT_P010:
        case DXGI_FORMAT_P016:
        case DXGI_FORMAT_Y210:
        case DXGI_FORMAT_Y216:
        case DXGI_FORMAT_NV11:
        case DXGI_FORMAT_AI44:
        case DXGI_FORMAT_IA44:
        case DXGI_FORMAT_A8P8:
        case DXGI_FORMAT_P208:
        case DXGI_FORMAT_V208:
        case DXGI_FORMAT_V408:
        case DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE:
        case DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE:
        case DXGI_FORMAT_FORCE_UINT: /* warning */
            break;
    }
    DEBUG_BREAKPOINT_TEST();
    return SVGA3D_BUFFER;
}


D3DDDIFORMAT vboxDXDxgiToDDIFormat(DXGI_FORMAT enmDxgiFormat)
{
    switch (enmDxgiFormat)
    {
        case DXGI_FORMAT_UNKNOWN:                    return D3DDDIFMT_UNKNOWN;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:         return D3DDDIFMT_A32B32G32R32F;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:         return D3DDDIFMT_A16B16G16R16F;
        case DXGI_FORMAT_R32G32_FLOAT:               return D3DDDIFMT_G32R32F;
        case DXGI_FORMAT_R10G10B10A2_UNORM:          return D3DDDIFMT_A2B10G10R10;
        case DXGI_FORMAT_R8G8B8A8_UNORM:             return D3DDDIFMT_A8B8G8R8;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:        return D3DDDIFMT_A8B8G8R8;
        case DXGI_FORMAT_R16G16_UNORM:               return D3DDDIFMT_G16R16;
        case DXGI_FORMAT_D32_FLOAT:                  return D3DDDIFMT_D32F_LOCKABLE;
        case DXGI_FORMAT_R32_FLOAT:                  return D3DDDIFMT_R32F;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:          return D3DDDIFMT_D24S8;
        case DXGI_FORMAT_R16_FLOAT:                  return D3DDDIFMT_R16F;
        case DXGI_FORMAT_D16_UNORM:                  return D3DDDIFMT_D16;
        case DXGI_FORMAT_R8G8_B8G8_UNORM:            return D3DDDIFMT_G8R8_G8B8;
        case DXGI_FORMAT_G8R8_G8B8_UNORM:            return D3DDDIFMT_R8G8_B8G8;
        case DXGI_FORMAT_BC1_UNORM:                  return D3DDDIFMT_DXT1;
        case DXGI_FORMAT_BC1_UNORM_SRGB:             return D3DDDIFMT_DXT1;
        case DXGI_FORMAT_BC2_UNORM:                  return D3DDDIFMT_DXT2;
        case DXGI_FORMAT_BC2_UNORM_SRGB:             return D3DDDIFMT_DXT2;
        case DXGI_FORMAT_BC3_UNORM:                  return D3DDDIFMT_DXT3;
        case DXGI_FORMAT_BC3_UNORM_SRGB:             return D3DDDIFMT_DXT3;
        case DXGI_FORMAT_BC4_UNORM:                  return D3DDDIFMT_DXT4;
        case DXGI_FORMAT_BC4_SNORM:                  return D3DDDIFMT_DXT4;
        case DXGI_FORMAT_BC5_UNORM:                  return D3DDDIFMT_DXT5;
        case DXGI_FORMAT_BC5_SNORM:                  return D3DDDIFMT_DXT5;
        case DXGI_FORMAT_B5G6R5_UNORM:               return D3DDDIFMT_R5G6B5;
        case DXGI_FORMAT_B5G5R5A1_UNORM:             return D3DDDIFMT_A1R5G5B5;
        case DXGI_FORMAT_B8G8R8A8_UNORM:             return D3DDDIFMT_A8R8G8B8;
        case DXGI_FORMAT_B8G8R8X8_UNORM:             return D3DDDIFMT_X8R8G8B8;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:        return D3DDDIFMT_A8R8G8B8;
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:        return D3DDDIFMT_X8R8G8B8;
        case DXGI_FORMAT_YUY2:                       return D3DDDIFMT_YUY2;
        case DXGI_FORMAT_P8:                         return D3DDDIFMT_P8;
        default:
            break;
    }
    return D3DDDIFMT_UNKNOWN;
}


static uint32_t vboxDXCalcResourceAllocationSize(PVBOXDX_RESOURCE pResource)
{
    /* The allocation holds the entire resource:
     *   (miplevel[0], ..., miplevel[MipLevels - 1])[0],
     *   ...,
     * (  miplevel[0], ..., miplevel[MipLevels - 1])[ArraySize - 1]
     */
    surf_size_struct base_level_size;
    base_level_size.width  = pResource->AllocationDesc.surfaceInfo.size.width;
    base_level_size.height = pResource->AllocationDesc.surfaceInfo.size.height;
    base_level_size.depth  = pResource->AllocationDesc.surfaceInfo.size.depth;

    return svga3dsurface_get_serialized_size_extended(pResource->AllocationDesc.surfaceInfo.format,
                                                      base_level_size,
                                                      pResource->AllocationDesc.surfaceInfo.numMipLevels,
                                                      pResource->AllocationDesc.surfaceInfo.arraySize,
                                                      pResource->AllocationDesc.surfaceInfo.multisampleCount);
}


static SVGA3dSurfaceAllFlags vboxDXCalcSurfaceFlags(const D3D11DDIARG_CREATERESOURCE *pCreateResource)
{
    SVGA3dSurfaceAllFlags f = 0;

    UINT const BindFlags = pCreateResource->BindFlags;
    Assert((BindFlags & (  D3D11_DDI_BIND_PIPELINE_MASK
                         & ~(  D3D10_DDI_BIND_VERTEX_BUFFER
                             | D3D10_DDI_BIND_INDEX_BUFFER
                             | D3D10_DDI_BIND_CONSTANT_BUFFER
                             | D3D10_DDI_BIND_SHADER_RESOURCE
                             | D3D10_DDI_BIND_STREAM_OUTPUT
                             | D3D10_DDI_BIND_RENDER_TARGET
                             | D3D10_DDI_BIND_DEPTH_STENCIL
                             | D3D11_DDI_BIND_UNORDERED_ACCESS
                             | D3D11_DDI_BIND_DECODER))) == 0);

    if (BindFlags & D3D10_DDI_BIND_VERTEX_BUFFER)
        f |= SVGA3D_SURFACE_BIND_VERTEX_BUFFER | SVGA3D_SURFACE_HINT_VERTEXBUFFER;
    if (BindFlags & D3D10_DDI_BIND_INDEX_BUFFER)
        f |= SVGA3D_SURFACE_BIND_INDEX_BUFFER | SVGA3D_SURFACE_HINT_INDEXBUFFER;
    if (BindFlags & D3D10_DDI_BIND_CONSTANT_BUFFER)
        f |= SVGA3D_SURFACE_BIND_CONSTANT_BUFFER;
    if (BindFlags & D3D10_DDI_BIND_SHADER_RESOURCE)
        f |= SVGA3D_SURFACE_BIND_SHADER_RESOURCE;
    if (BindFlags & D3D10_DDI_BIND_STREAM_OUTPUT)
        f |= SVGA3D_SURFACE_BIND_STREAM_OUTPUT;
    if (BindFlags & D3D10_DDI_BIND_RENDER_TARGET)
        f |= SVGA3D_SURFACE_BIND_RENDER_TARGET | SVGA3D_SURFACE_HINT_RENDERTARGET;
    if (BindFlags & D3D10_DDI_BIND_DEPTH_STENCIL)
        f |= SVGA3D_SURFACE_BIND_DEPTH_STENCIL | SVGA3D_SURFACE_HINT_DEPTHSTENCIL;
    if (BindFlags & D3D11_DDI_BIND_UNORDERED_ACCESS)
        f |= SVGA3D_SURFACE_BIND_UAVIEW;
    if (BindFlags & D3D11_DDI_BIND_DECODER)
        f |= SVGA3D_SURFACE_RESERVED1;

    /* D3D10_DDI_BIND_PRESENT textures can be used as render targets in a blitter on the host. */
    if (BindFlags & D3D10_DDI_BIND_PRESENT)
        f |= SVGA3D_SURFACE_SCREENTARGET | SVGA3D_SURFACE_BIND_RENDER_TARGET | SVGA3D_SURFACE_HINT_RENDERTARGET;

    D3D10_DDI_RESOURCE_USAGE const Usage = (D3D10_DDI_RESOURCE_USAGE)pCreateResource->Usage;
    if (Usage == D3D10_DDI_USAGE_DEFAULT)
        f |= SVGA3D_SURFACE_HINT_INDIRECT_UPDATE;
    else if (Usage == D3D10_DDI_USAGE_IMMUTABLE)
        f |= SVGA3D_SURFACE_HINT_STATIC;
    else if (Usage == D3D10_DDI_USAGE_DYNAMIC)
        f |= SVGA3D_SURFACE_HINT_DYNAMIC;
    else if (Usage == D3D10_DDI_USAGE_STAGING)
        f |= SVGA3D_SURFACE_STAGING_UPLOAD | SVGA3D_SURFACE_STAGING_DOWNLOAD;

    D3D10DDIRESOURCE_TYPE const ResourceDimension = pCreateResource->ResourceDimension;
    if (ResourceDimension == D3D10DDIRESOURCE_TEXTURE1D)
        f |= SVGA3D_SURFACE_1D | SVGA3D_SURFACE_HINT_TEXTURE;
    else if (ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D)
        f |= SVGA3D_SURFACE_HINT_TEXTURE;
    else if (ResourceDimension == D3D10DDIRESOURCE_TEXTURE3D)
        f |= SVGA3D_SURFACE_VOLUME | SVGA3D_SURFACE_HINT_TEXTURE;
    else if (ResourceDimension == D3D10DDIRESOURCE_TEXTURECUBE)
        f |= SVGA3D_SURFACE_CUBEMAP | SVGA3D_SURFACE_HINT_TEXTURE;

    UINT const MiscFlags = pCreateResource->MiscFlags;
    if (MiscFlags & D3D11_DDI_RESOURCE_MISC_DRAWINDIRECT_ARGS)
        f |= SVGA3D_SURFACE_DRAWINDIRECT_ARGS;
    if (MiscFlags & D3D11_DDI_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS)
        f |= SVGA3D_SURFACE_BIND_RAW_VIEWS; /*  */
    if (MiscFlags & D3D11_DDI_RESOURCE_MISC_BUFFER_STRUCTURED)
        f |= SVGA3D_SURFACE_BUFFER_STRUCTURED;
    if (MiscFlags & D3D11_DDI_RESOURCE_MISC_RESOURCE_CLAMP)
        f |= SVGA3D_SURFACE_RESOURCE_CLAMP;

    if (pCreateResource->SampleDesc.Count > 1)
        f |= SVGA3D_SURFACE_MULTISAMPLE;

    return f;
}


static D3D10_DDI_RESOURCE_USAGE vboxDXSurfaceFlagsToResourceUsage(SVGA3dSurfaceAllFlags surfaceFlags)
{
    if (surfaceFlags & SVGA3D_SURFACE_HINT_INDIRECT_UPDATE) return D3D10_DDI_USAGE_DEFAULT;
    if (surfaceFlags & SVGA3D_SURFACE_HINT_STATIC)          return D3D10_DDI_USAGE_IMMUTABLE;
    if (surfaceFlags & SVGA3D_SURFACE_HINT_DYNAMIC)         return D3D10_DDI_USAGE_DYNAMIC;
    if (surfaceFlags & (SVGA3D_SURFACE_STAGING_UPLOAD | SVGA3D_SURFACE_STAGING_DOWNLOAD))
                                                            return D3D10_DDI_USAGE_STAGING;
    AssertFailedReturn(D3D10_DDI_USAGE_STAGING);
}


static D3D10DDIRESOURCE_TYPE vboxDXSurfaceFlagsToResourceDimension(SVGA3dSurfaceAllFlags surfaceFlags)
{
    if (surfaceFlags & SVGA3D_SURFACE_1D)           return D3D10DDIRESOURCE_TEXTURE1D;
    if (surfaceFlags & SVGA3D_SURFACE_VOLUME)       return D3D10DDIRESOURCE_TEXTURE3D;
    if (surfaceFlags & SVGA3D_SURFACE_CUBEMAP)      return D3D10DDIRESOURCE_TEXTURECUBE;
    if (surfaceFlags & SVGA3D_SURFACE_HINT_TEXTURE) return D3D10DDIRESOURCE_TEXTURE2D;
    /** @todo D3D11DDIRESOURCE_BUFFEREX? */
    return D3D10DDIRESOURCE_BUFFER;
}


int vboxDXInitResourceData(PVBOXDX_RESOURCE pResource, const D3D11DDIARG_CREATERESOURCE *pCreateResource)
{
    /* Store data which might be needed later. */
    pResource->ResourceDimension = pCreateResource->ResourceDimension;
    pResource->Usage             = (D3D10_DDI_RESOURCE_USAGE)pCreateResource->Usage;
    for (UINT i = 0; i < pCreateResource->MipLevels; ++i)
        pResource->aMipInfoList[i] = pCreateResource->pMipInfoList[i];

    /* Init surface information which will be used by the miniport to define the surface. */
    VBOXDXALLOCATIONDESC *pDesc = &pResource->AllocationDesc;
    pDesc->surfaceInfo.surfaceFlags       = vboxDXCalcSurfaceFlags(pCreateResource);
    pDesc->surfaceInfo.format             = vboxDXDxgiToSvgaFormat(pCreateResource->Format);
    pDesc->surfaceInfo.numMipLevels       = pCreateResource->MipLevels;
    pDesc->surfaceInfo.multisampleCount   = pCreateResource->SampleDesc.Count;
    if (pDesc->surfaceInfo.multisampleCount > 1)
    {
        pDesc->surfaceInfo.multisamplePattern = SVGA3D_MS_PATTERN_STANDARD;
        pDesc->surfaceInfo.qualityLevel       = SVGA3D_MS_QUALITY_FULL;
    }
    else
    {
        pDesc->surfaceInfo.multisamplePattern = SVGA3D_MS_PATTERN_NONE;
        pDesc->surfaceInfo.qualityLevel       = SVGA3D_MS_QUALITY_NONE;
    }
    pDesc->surfaceInfo.autogenFilter      = SVGA3D_TEX_FILTER_NONE;
    pDesc->surfaceInfo.size.width         = pCreateResource->pMipInfoList[0].TexelWidth;
    pDesc->surfaceInfo.size.height        = pCreateResource->pMipInfoList[0].TexelHeight;
    pDesc->surfaceInfo.size.depth         = pCreateResource->pMipInfoList[0].TexelDepth;
    pDesc->surfaceInfo.arraySize          = pCreateResource->ArraySize;
    pDesc->surfaceInfo.bufferByteStride   = pCreateResource->ByteStride;
    if (pCreateResource->pPrimaryDesc)
    {
         pDesc->fPrimary                  = true;
         pDesc->PrimaryDesc               = *pCreateResource->pPrimaryDesc;
    }
    else
         pDesc->fPrimary                  = false;
    pDesc->enmDDIFormat                   = vboxDXDxgiToDDIFormat(pCreateResource->Format);
    pDesc->resourceInfo.BindFlags         = pCreateResource->BindFlags;
    pDesc->resourceInfo.MapFlags          = pCreateResource->MapFlags;
    pDesc->resourceInfo.MiscFlags         = pCreateResource->MiscFlags;
    pDesc->resourceInfo.Format            = pCreateResource->Format;
    pDesc->resourceInfo.DecoderBufferType = pCreateResource->DecoderBufferType;

    /* Finally set the allocation type and compute the size. */
    pDesc->enmAllocationType = VBOXDXALLOCATIONTYPE_SURFACE;
    pDesc->cbAllocation = vboxDXCalcResourceAllocationSize(pResource);

    /* Init remaining fields. */
    pResource->cSubresources = pCreateResource->MipLevels * pCreateResource->ArraySize;
    pResource->pKMResource = NULL;
    pResource->uMap = 0;
    RTListInit(&pResource->listSRV);
    RTListInit(&pResource->listRTV);
    RTListInit(&pResource->listDSV);
    RTListInit(&pResource->listUAV);
    RTListInit(&pResource->listVDOV);
    RTListInit(&pResource->listVPIV);
    RTListInit(&pResource->listVPOV);

    return VINF_SUCCESS;
}


static HRESULT dxAllocate(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource, D3DKMT_HANDLE *phAllocation)
{
    D3DDDI_ALLOCATIONINFO2 ddiAllocationInfo;
    RT_ZERO(ddiAllocationInfo);
    //ddiAllocationInfo.pSystemMem            = 0;
    ddiAllocationInfo.pPrivateDriverData    = &pResource->AllocationDesc;
    ddiAllocationInfo.PrivateDriverDataSize = sizeof(pResource->AllocationDesc);
    if (pResource->AllocationDesc.fPrimary)
    {
        ddiAllocationInfo.VidPnSourceId     = pResource->AllocationDesc.PrimaryDesc.VidPnSourceId;
        ddiAllocationInfo.Flags.Primary     = pResource->AllocationDesc.fPrimary;
    }

    D3DDDICB_ALLOCATE ddiAllocate;
    RT_ZERO(ddiAllocate);
    //ddiAllocate.pPrivateDriverData    = NULL;
    //ddiAllocate.PrivateDriverDataSize = 0;
    ddiAllocate.hResource             = pResource->hRTResource.handle;
    ddiAllocate.NumAllocations        = 1;
    ddiAllocate.pAllocationInfo2      = &ddiAllocationInfo;

    HRESULT hr = pDevice->pRTCallbacks->pfnAllocateCb(pDevice->hRTDevice.handle, &ddiAllocate);
    LogFlowFunc((" pfnAllocateCb returned %d, hKMResource 0x%X, hAllocation 0x%X", hr, ddiAllocate.hKMResource, ddiAllocationInfo.hAllocation));

    if (SUCCEEDED(hr))
        *phAllocation = ddiAllocationInfo.hAllocation;

    return hr;
}


bool vboxDXCreateResource(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource,
                          const D3D11DDIARG_CREATERESOURCE *pCreateResource)
{
    pResource->pKMResource = (PVBOXDXKMRESOURCE)RTMemAllocZ(sizeof(VBOXDXKMRESOURCE));
    AssertReturnStmt(pResource->pKMResource, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY), false);

    D3DKMT_HANDLE hAllocation = 0;
    HRESULT hr = dxAllocate(pDevice, pResource, &hAllocation);
    if (FAILED(hr))
    {
        /* Might be not enough memory due to temporary staging buffers. */
        vboxDXFlush(pDevice, true);
        hr = dxAllocate(pDevice, pResource, &hAllocation);
    }
    AssertReturnStmt(SUCCEEDED(hr), RTMemFree(pResource->pKMResource); vboxDXDeviceSetError(pDevice, hr), false);

    pResource->pKMResource->pResource = pResource;
    pResource->pKMResource->hAllocation = hAllocation;
    RTListAppend(&pDevice->listResources, &pResource->pKMResource->nodeResource);

    if (pCreateResource->pInitialDataUP)
    {
        /* Upload the data to the resource. */
        for (unsigned i = 0; i < pResource->cSubresources; ++i)
            vboxDXResourceUpdateSubresourceUP(pDevice, pResource, i, NULL,
                                              pCreateResource->pInitialDataUP[i].pSysMem,
                                              pCreateResource->pInitialDataUP[i].SysMemPitch,
                                              pCreateResource->pInitialDataUP[i].SysMemSlicePitch, 0);

    }
    else
    {
        /** @todo Test Lock/Unlock. Not sure if memset is really necessary. */
        if (   pResource->Usage == D3D10_DDI_USAGE_DYNAMIC
            || pResource->Usage == D3D10_DDI_USAGE_STAGING)
        {
            /* Zero the allocation. */
            D3DDDICB_LOCK ddiLock;
            RT_ZERO(ddiLock);
            ddiLock.hAllocation = vboxDXGetAllocation(pResource);
            ddiLock.Flags.WriteOnly = 1;
            hr = pDevice->pRTCallbacks->pfnLockCb(pDevice->hRTDevice.handle, &ddiLock);
            if (SUCCEEDED(hr))
            {
                memset(ddiLock.pData, 0, pResource->AllocationDesc.cbAllocation);

                hAllocation = vboxDXGetAllocation(pResource);

                D3DDDICB_UNLOCK ddiUnlock;
                ddiUnlock.NumAllocations = 1;
                ddiUnlock.phAllocations = &hAllocation;
                hr = pDevice->pRTCallbacks->pfnUnlockCb(pDevice->hRTDevice.handle, &ddiUnlock);
            }
        }
    }

    return true;
}


bool vboxDXOpenResource(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource,
                        const D3D10DDIARG_OPENRESOURCE *pOpenResource)
{
    AssertReturnStmt(pOpenResource->NumAllocations == 1,
                     vboxDXDeviceSetError(pDevice, E_INVALIDARG), false);
    AssertReturnStmt(pOpenResource->pOpenAllocationInfo2[0].PrivateDriverDataSize == sizeof(VBOXDXALLOCATIONDESC),
                     vboxDXDeviceSetError(pDevice, E_INVALIDARG), false);

    pResource->pKMResource = (PVBOXDXKMRESOURCE)RTMemAllocZ(sizeof(VBOXDXKMRESOURCE));
    AssertReturnStmt(pResource->pKMResource, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY), false);

    VBOXDXALLOCATIONDESC const *pDesc = (VBOXDXALLOCATIONDESC *)pOpenResource->pOpenAllocationInfo2[0].pPrivateDriverData;

    /* Restore resource data. */
    pResource->ResourceDimension = vboxDXSurfaceFlagsToResourceDimension(pDesc->surfaceInfo.surfaceFlags);
    pResource->Usage             = vboxDXSurfaceFlagsToResourceUsage(pDesc->surfaceInfo.surfaceFlags);
    for (UINT i = 0; i < pDesc->surfaceInfo.numMipLevels; ++i)
        RT_ZERO(pResource->aMipInfoList[i]);

    pResource->AllocationDesc = *pDesc;
    pResource->AllocationDesc.resourceInfo.MiscFlags |= D3D10_DDI_RESOURCE_MISC_SHARED;

    /* Init remaining fields. */
    pResource->cSubresources = pDesc->surfaceInfo.numMipLevels * pDesc->surfaceInfo.arraySize;
    pResource->uMap             = 0;
    RTListInit(&pResource->listSRV);
    RTListInit(&pResource->listRTV);
    RTListInit(&pResource->listDSV);
    RTListInit(&pResource->listUAV);
    RTListInit(&pResource->listVDOV);
    RTListInit(&pResource->listVPIV);
    RTListInit(&pResource->listVPOV);

    pResource->pKMResource->pResource = pResource;
    pResource->pKMResource->hAllocation = pOpenResource->pOpenAllocationInfo2[0].hAllocation;
    RTListAppend(&pDevice->listResources, &pResource->pKMResource->nodeResource);
    return true;
}


/* Destroy a resource created by the system (via DDI). Primary resources are freed immediately.
 * Other resources are moved to the deferred destruction queue (pDevice->listDestroyedResources).
 * The 'pResource' structure will be deleted by D3D runtime in any case.
 */
void vboxDXDestroyResource(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource)
{
    /* "the driver must process its deferred-destruction queue during calls to its Flush(D3D10) function"
     * "Primary destruction cannot be deferred by the Direct3D runtime, and the driver must call
     * the pfnDeallocateCb function appropriately within a call to the driver's DestroyResource(D3D10) function."
     */

    Assert(RTListIsEmpty(&pResource->listSRV));
    Assert(RTListIsEmpty(&pResource->listRTV));
    Assert(RTListIsEmpty(&pResource->listDSV));
    Assert(RTListIsEmpty(&pResource->listUAV));
    Assert(RTListIsEmpty(&pResource->listVDOV));
    Assert(RTListIsEmpty(&pResource->listVPIV));
    Assert(RTListIsEmpty(&pResource->listVPOV));

    /* Remove from the list of active resources. */
    RTListNodeRemove(&pResource->pKMResource->nodeResource);

    if (pResource->AllocationDesc.fPrimary)
    {
        /* Delete immediately. */
        D3DDDICB_DEALLOCATE ddiDeallocate;
        RT_ZERO(ddiDeallocate);
        //ddiDeallocate.hResource      = NULL;
        ddiDeallocate.NumAllocations = 1;
        ddiDeallocate.HandleList     = &pResource->pKMResource->hAllocation;

        HRESULT hr = pDevice->pRTCallbacks->pfnDeallocateCb(pDevice->hRTDevice.handle, &ddiDeallocate);
        LogFlowFunc(("pfnDeallocateCb returned %d", hr));
        AssertStmt(SUCCEEDED(hr), vboxDXDeviceSetError(pDevice, hr));

        RTMemFree(pResource->pKMResource);
    }
    else
    {
        if (!RT_BOOL(pResource->AllocationDesc.resourceInfo.MiscFlags & D3D10_DDI_RESOURCE_MISC_SHARED))
        {
            /* Set the resource for deferred destruction. */
            pResource->pKMResource->pResource = NULL;
            RTListAppend(&pDevice->listDestroyedResources, &pResource->pKMResource->nodeResource);
        }
        else
        {
            /* Opened shared resources must not be actually deleted. Just free the KM structure. */
            RTMemFree(pResource->pKMResource);
        }
    }
}


static SVGA3dDX11LogicOp d3dToSvgaLogicOp(D3D11_1_DDI_LOGIC_OP LogicOp)
{
    switch (LogicOp)
    {
        case D3D11_1_DDI_LOGIC_OP_CLEAR:         return SVGA3D_DX11_LOGICOP_CLEAR;
        case D3D11_1_DDI_LOGIC_OP_SET:           return SVGA3D_DX11_LOGICOP_SET;
        case D3D11_1_DDI_LOGIC_OP_COPY:          return SVGA3D_DX11_LOGICOP_COPY;
        case D3D11_1_DDI_LOGIC_OP_COPY_INVERTED: return SVGA3D_DX11_LOGICOP_COPY_INVERTED;
        case D3D11_1_DDI_LOGIC_OP_NOOP:          return SVGA3D_DX11_LOGICOP_NOOP;
        case D3D11_1_DDI_LOGIC_OP_INVERT:        return SVGA3D_DX11_LOGICOP_INVERT;
        case D3D11_1_DDI_LOGIC_OP_AND:           return SVGA3D_DX11_LOGICOP_AND;
        case D3D11_1_DDI_LOGIC_OP_NAND:          return SVGA3D_DX11_LOGICOP_NAND;
        case D3D11_1_DDI_LOGIC_OP_OR:            return SVGA3D_DX11_LOGICOP_OR;
        case D3D11_1_DDI_LOGIC_OP_NOR:           return SVGA3D_DX11_LOGICOP_NOR;
        case D3D11_1_DDI_LOGIC_OP_XOR:           return SVGA3D_DX11_LOGICOP_XOR;
        case D3D11_1_DDI_LOGIC_OP_EQUIV:         return SVGA3D_DX11_LOGICOP_EQUIV;
        case D3D11_1_DDI_LOGIC_OP_AND_REVERSE:   return SVGA3D_DX11_LOGICOP_AND_REVERSE;
        case D3D11_1_DDI_LOGIC_OP_AND_INVERTED:  return SVGA3D_DX11_LOGICOP_AND_INVERTED;
        case D3D11_1_DDI_LOGIC_OP_OR_REVERSE:    return SVGA3D_DX11_LOGICOP_OR_REVERSE;
        case D3D11_1_DDI_LOGIC_OP_OR_INVERTED:   return SVGA3D_DX11_LOGICOP_OR_INVERTED;
        default:
            break;
    }
    AssertFailed();
    return SVGA3D_DX11_LOGICOP_COPY;
}


static SVGA3dBlendOp d3dToSvgaBlend(D3D10_DDI_BLEND Blend)
{
    switch (Blend)
    {
        case D3D10_DDI_BLEND_ZERO:            return SVGA3D_BLENDOP_ZERO;
        case D3D10_DDI_BLEND_ONE:             return SVGA3D_BLENDOP_ONE;
        case D3D10_DDI_BLEND_SRC_COLOR:       return SVGA3D_BLENDOP_SRCCOLOR;
        case D3D10_DDI_BLEND_INV_SRC_COLOR:   return SVGA3D_BLENDOP_INVSRCCOLOR;
        case D3D10_DDI_BLEND_SRC_ALPHA:       return SVGA3D_BLENDOP_SRCALPHA;
        case D3D10_DDI_BLEND_INV_SRC_ALPHA:   return SVGA3D_BLENDOP_INVSRCALPHA;
        case D3D10_DDI_BLEND_DEST_ALPHA:      return SVGA3D_BLENDOP_DESTALPHA;
        case D3D10_DDI_BLEND_INV_DEST_ALPHA:  return SVGA3D_BLENDOP_INVDESTALPHA;
        case D3D10_DDI_BLEND_DEST_COLOR:      return SVGA3D_BLENDOP_DESTCOLOR;
        case D3D10_DDI_BLEND_INV_DEST_COLOR:  return SVGA3D_BLENDOP_INVDESTCOLOR;
        case D3D10_DDI_BLEND_SRC_ALPHASAT:    return SVGA3D_BLENDOP_SRCALPHASAT;
        case D3D10_DDI_BLEND_BLEND_FACTOR:    return SVGA3D_BLENDOP_BLENDFACTOR;
        case D3D10_DDI_BLEND_INVBLEND_FACTOR: return SVGA3D_BLENDOP_INVBLENDFACTOR;
        case D3D10_DDI_BLEND_SRC1_COLOR:      return SVGA3D_BLENDOP_SRC1COLOR;
        case D3D10_DDI_BLEND_INV_SRC1_COLOR:  return SVGA3D_BLENDOP_INVSRC1COLOR;
        case D3D10_DDI_BLEND_SRC1_ALPHA:      return SVGA3D_BLENDOP_SRC1ALPHA;
        case D3D10_DDI_BLEND_INV_SRC1_ALPHA:  return SVGA3D_BLENDOP_INVSRC1ALPHA;
        default:
            break;
    }
    AssertFailed();
    return SVGA3D_BLENDOP_ZERO;
}


static SVGA3dBlendEquation d3dToSvgaBlendEq(D3D10_DDI_BLEND_OP BlendOp)
{
    switch (BlendOp)
    {
        case D3D10_DDI_BLEND_OP_ADD:             return SVGA3D_BLENDEQ_ADD;
        case D3D10_DDI_BLEND_OP_SUBTRACT:        return SVGA3D_BLENDEQ_SUBTRACT;
        case D3D10_DDI_BLEND_OP_REV_SUBTRACT:    return SVGA3D_BLENDEQ_REVSUBTRACT;
        case D3D10_DDI_BLEND_OP_MIN:             return SVGA3D_BLENDEQ_MINIMUM;
        case D3D10_DDI_BLEND_OP_MAX:             return SVGA3D_BLENDEQ_MAXIMUM;
        default:
            break;
    }
    AssertFailed();
    return SVGA3D_BLENDEQ_ADD;
}


void vboxDXCreateBlendState(PVBOXDX_DEVICE pDevice, PVBOXDX_BLENDSTATE pBlendState)
{
    int rc = RTHandleTableAlloc(pDevice->hHTBlendState, pBlendState, &pBlendState->uBlendId);
    AssertRCReturnVoidStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    D3D11_1_DDI_BLEND_DESC const *pBlendDesc = &pBlendState->BlendDesc;
    SVGA3dDXBlendStatePerRT perRT[SVGA3D_MAX_RENDER_TARGETS];
    AssertCompile(SVGA3D_MAX_RENDER_TARGETS == D3D10_DDI_SIMULTANEOUS_RENDER_TARGET_COUNT);

    for (unsigned i = 0; i < D3D10_DDI_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        perRT[i].blendEnable           = pBlendDesc->RenderTarget[i].BlendEnable;
        perRT[i].srcBlend              = (uint8_t)d3dToSvgaBlend(pBlendDesc->RenderTarget[i].SrcBlend);
        perRT[i].destBlend             = (uint8_t)d3dToSvgaBlend(pBlendDesc->RenderTarget[i].DestBlend);
        perRT[i].blendOp               = (uint8_t)d3dToSvgaBlendEq(pBlendDesc->RenderTarget[i].BlendOp);
        perRT[i].srcBlendAlpha         = (uint8_t)d3dToSvgaBlend(pBlendDesc->RenderTarget[i].SrcBlendAlpha);
        perRT[i].destBlendAlpha        = (uint8_t)d3dToSvgaBlend(pBlendDesc->RenderTarget[i].DestBlendAlpha);
        perRT[i].blendOpAlpha          = (uint8_t)d3dToSvgaBlendEq(pBlendDesc->RenderTarget[i].BlendOpAlpha);
        perRT[i].renderTargetWriteMask = pBlendDesc->RenderTarget[i].RenderTargetWriteMask;
        perRT[i].logicOpEnable         = pBlendDesc->RenderTarget[i].LogicOpEnable;
        perRT[i].logicOp               = (uint8_t)d3dToSvgaLogicOp(pBlendDesc->RenderTarget[i].LogicOp);
    }

    vgpu10DefineBlendState(pDevice,
                           pBlendState->uBlendId,
                           pBlendDesc->AlphaToCoverageEnable,
                           pBlendDesc->IndependentBlendEnable,
                           perRT);
}


void vboxDXDestroyBlendState(PVBOXDX_DEVICE pDevice, PVBOXDX_BLENDSTATE pBlendState)
{
    vgpu10DestroyBlendState(pDevice, pBlendState->uBlendId);
    RTHandleTableFree(pDevice->hHTBlendState, pBlendState->uBlendId);
}


static SVGA3dComparisonFunc d3dToSvgaComparisonFunc(D3D10_DDI_COMPARISON_FUNC DepthFunc)
{
    switch (DepthFunc)
    {
        case D3D10_DDI_COMPARISON_NEVER:         return SVGA3D_COMPARISON_NEVER;
        case D3D10_DDI_COMPARISON_LESS:          return SVGA3D_COMPARISON_LESS;
        case D3D10_DDI_COMPARISON_EQUAL:         return SVGA3D_COMPARISON_EQUAL;
        case D3D10_DDI_COMPARISON_LESS_EQUAL:    return SVGA3D_COMPARISON_LESS_EQUAL;
        case D3D10_DDI_COMPARISON_GREATER:       return SVGA3D_COMPARISON_GREATER;
        case D3D10_DDI_COMPARISON_NOT_EQUAL:     return SVGA3D_COMPARISON_NOT_EQUAL;
        case D3D10_DDI_COMPARISON_GREATER_EQUAL: return SVGA3D_COMPARISON_GREATER_EQUAL;
        case D3D10_DDI_COMPARISON_ALWAYS:        return SVGA3D_COMPARISON_ALWAYS;
        default:
            break;
    }
    AssertFailed();
    return SVGA3D_COMPARISON_LESS;
}


static uint8_t d3dToSvgaStencilOp(D3D10_DDI_STENCIL_OP StencilOp)
{
    switch (StencilOp)
    {
        case D3D10_DDI_STENCIL_OP_KEEP:     return SVGA3D_STENCILOP_KEEP;
        case D3D10_DDI_STENCIL_OP_ZERO:     return SVGA3D_STENCILOP_ZERO;
        case D3D10_DDI_STENCIL_OP_REPLACE:  return SVGA3D_STENCILOP_REPLACE;
        case D3D10_DDI_STENCIL_OP_INCR_SAT: return SVGA3D_STENCILOP_INCRSAT;
        case D3D10_DDI_STENCIL_OP_DECR_SAT: return SVGA3D_STENCILOP_DECRSAT;
        case D3D10_DDI_STENCIL_OP_INVERT:   return SVGA3D_STENCILOP_INVERT;
        case D3D10_DDI_STENCIL_OP_INCR:     return SVGA3D_STENCILOP_INCR;
        case D3D10_DDI_STENCIL_OP_DECR:     return SVGA3D_STENCILOP_DECR;
        default:
            break;
    }
    AssertFailed();
    return SVGA3D_STENCILOP_KEEP;
}


void vboxDXCreateDepthStencilState(PVBOXDX_DEVICE pDevice, PVBOXDX_DEPTHSTENCIL_STATE pDepthStencilState)
{
    int rc = RTHandleTableAlloc(pDevice->hHTDepthStencilState, pDepthStencilState, &pDepthStencilState->uDepthStencilId);
    AssertRCReturnVoidStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    D3D10_DDI_DEPTH_STENCIL_DESC *p = &pDepthStencilState->DepthStencilDesc;
    uint8_t depthEnable                   = p->DepthEnable;
    SVGA3dDepthWriteMask depthWriteMask   = p->DepthWriteMask;
    SVGA3dComparisonFunc depthFunc        = d3dToSvgaComparisonFunc(p->DepthFunc);
    uint8 stencilEnable                   = p->StencilEnable;
    uint8 frontEnable                     = p->FrontEnable;
    uint8 backEnable                      = p->BackEnable;
    uint8 stencilReadMask                 = p->StencilReadMask;
    uint8 stencilWriteMask                = p->StencilWriteMask;

    uint8 frontStencilFailOp              = d3dToSvgaStencilOp(p->FrontFace.StencilFailOp);
    uint8 frontStencilDepthFailOp         = d3dToSvgaStencilOp(p->FrontFace.StencilDepthFailOp);
    uint8 frontStencilPassOp              = d3dToSvgaStencilOp(p->FrontFace.StencilPassOp);
    SVGA3dComparisonFunc frontStencilFunc = d3dToSvgaComparisonFunc(p->FrontFace.StencilFunc);

    uint8 backStencilFailOp               = d3dToSvgaStencilOp(p->BackFace.StencilFailOp);
    uint8 backStencilDepthFailOp          = d3dToSvgaStencilOp(p->BackFace.StencilDepthFailOp);
    uint8 backStencilPassOp               = d3dToSvgaStencilOp(p->BackFace.StencilPassOp);
    SVGA3dComparisonFunc backStencilFunc  = d3dToSvgaComparisonFunc(p->BackFace.StencilFunc);

    vgpu10DefineDepthStencilState(pDevice,
                                  pDepthStencilState->uDepthStencilId,
                                  depthEnable,
                                  depthWriteMask,
                                  depthFunc,
                                  stencilEnable,
                                  frontEnable,
                                  backEnable,
                                  stencilReadMask,
                                  stencilWriteMask,
                                  frontStencilFailOp,
                                  frontStencilDepthFailOp,
                                  frontStencilPassOp,
                                  frontStencilFunc,
                                  backStencilFailOp,
                                  backStencilDepthFailOp,
                                  backStencilPassOp,
                                  backStencilFunc);
}


void vboxDXDestroyDepthStencilState(PVBOXDX_DEVICE pDevice, PVBOXDX_DEPTHSTENCIL_STATE pDepthStencilState)
{
    vgpu10DestroyDepthStencilState(pDevice, pDepthStencilState->uDepthStencilId);
    RTHandleTableFree(pDevice->hHTDepthStencilState, pDepthStencilState->uDepthStencilId);
}


static uint8_t d3dToSvgaFillMode(D3D10_DDI_FILL_MODE FillMode)
{
    switch (FillMode)
    {
        case D3D10_DDI_FILL_WIREFRAME: return SVGA3D_FILLMODE_LINE;
        case D3D10_DDI_FILL_SOLID:     return SVGA3D_FILLMODE_FILL;
        default:
            break;
    }
    AssertFailed();
    return SVGA3D_FILLMODE_FILL;
}


static SVGA3dCullMode d3dToSvgaCullMode(D3D10_DDI_CULL_MODE CullMode)
{
    switch (CullMode)
    {
        case D3D10_DDI_CULL_NONE:  return SVGA3D_CULL_NONE;
        case D3D10_DDI_CULL_FRONT: return SVGA3D_CULL_FRONT;
        case D3D10_DDI_CULL_BACK:  return SVGA3D_CULL_BACK;
        default:
            break;
    }
    AssertFailed();
    return SVGA3D_CULL_NONE;
}


void vboxDXCreateRasterizerState(PVBOXDX_DEVICE pDevice, PVBOXDX_RASTERIZER_STATE pRasterizerState)
{
    int rc = RTHandleTableAlloc(pDevice->hHTRasterizerState, pRasterizerState, &pRasterizerState->uRasterizerId);
    AssertRCReturnVoidStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    D3D11_1_DDI_RASTERIZER_DESC *p = &pRasterizerState->RasterizerDesc;
    uint8 fillMode              = d3dToSvgaFillMode(p->FillMode);
    SVGA3dCullMode cullMode     = d3dToSvgaCullMode(p->CullMode);
    uint8 frontCounterClockwise = p->FrontCounterClockwise;
    uint8 provokingVertexLast   = 0; /** @todo */
    int32 depthBias             = p->DepthBias;
    float depthBiasClamp        = p->DepthBiasClamp;
    float slopeScaledDepthBias  = p->SlopeScaledDepthBias;
    uint8 depthClipEnable       = p->DepthClipEnable;
    uint8 scissorEnable         = p->ScissorEnable;
    SVGA3dMultisampleRastEnable multisampleEnable = p->MultisampleEnable;
    uint8 antialiasedLineEnable = p->AntialiasedLineEnable;
    float lineWidth             = 1.0f; /** @todo */
    uint8 lineStippleEnable     = 0; /** @todo */
    uint8 lineStippleFactor     = 0; /** @todo */
    uint16 lineStipplePattern   = 0; /** @todo */
    uint32 forcedSampleCount    = p->ForcedSampleCount;

    if (pDevice->pAdapter->fVBoxCaps & VBSVGA3D_CAP_RASTERIZER_STATE_V2)
    {
        vgpu10DefineRasterizerState_v2(pDevice,
                                       pRasterizerState->uRasterizerId,
                                       fillMode,
                                       cullMode,
                                       frontCounterClockwise,
                                       provokingVertexLast,
                                       depthBias,
                                       depthBiasClamp,
                                       slopeScaledDepthBias,
                                       depthClipEnable,
                                       scissorEnable,
                                       multisampleEnable,
                                       antialiasedLineEnable,
                                       lineWidth,
                                       lineStippleEnable,
                                       lineStippleFactor,
                                       lineStipplePattern,
                                       forcedSampleCount);
    }
    else
    {
        vgpu10DefineRasterizerState(pDevice,
                                    pRasterizerState->uRasterizerId,
                                    fillMode,
                                    cullMode,
                                    frontCounterClockwise,
                                    provokingVertexLast,
                                    depthBias,
                                    depthBiasClamp,
                                    slopeScaledDepthBias,
                                    depthClipEnable,
                                    scissorEnable,
                                    multisampleEnable,
                                    antialiasedLineEnable,
                                    lineWidth,
                                    lineStippleEnable,
                                    lineStippleFactor,
                                    lineStipplePattern);
    }
}


void vboxDXDestroyRasterizerState(PVBOXDX_DEVICE pDevice, PVBOXDX_RASTERIZER_STATE pRasterizerState)
{
    vgpu10DestroyRasterizerState(pDevice, pRasterizerState->uRasterizerId);
    RTHandleTableFree(pDevice->hHTRasterizerState, pRasterizerState->uRasterizerId);
}


static SVGA3dFilter d3dToSvgaFilter(D3D10_DDI_FILTER Filter)
{
    SVGA3dFilter f = 0;

    if (D3D10_DDI_DECODE_MIP_FILTER(Filter) == D3D10_DDI_FILTER_TYPE_LINEAR)
        f |= SVGA3D_FILTER_MIP_LINEAR;
    if (D3D10_DDI_DECODE_MAG_FILTER(Filter) == D3D10_DDI_FILTER_TYPE_LINEAR)
        f |= SVGA3D_FILTER_MAG_LINEAR;
    if (D3D10_DDI_DECODE_MIN_FILTER(Filter) == D3D10_DDI_FILTER_TYPE_LINEAR)
        f |= SVGA3D_FILTER_MIN_LINEAR;
    if (D3D10_DDI_DECODE_IS_ANISOTROPIC_FILTER(Filter))
        f |= SVGA3D_FILTER_ANISOTROPIC;
    if (D3D10_DDI_DECODE_IS_COMPARISON_FILTER(Filter))
        f |= SVGA3D_FILTER_COMPARE;
    Assert(D3DWDDM1_3DDI_DECODE_FILTER_REDUCTION(Filter) <= D3DWDDM1_3DDI_FILTER_REDUCTION_TYPE_COMPARISON);
    return f;
}


static uint8 d3dToSvgaTextureAddressMode(D3D10_DDI_TEXTURE_ADDRESS_MODE AddressMode)
{
    switch (AddressMode)
    {
        case D3D10_DDI_TEXTURE_ADDRESS_WRAP:       return SVGA3D_TEX_ADDRESS_WRAP;
        case D3D10_DDI_TEXTURE_ADDRESS_MIRROR:     return SVGA3D_TEX_ADDRESS_MIRROR;
        case D3D10_DDI_TEXTURE_ADDRESS_CLAMP:      return SVGA3D_TEX_ADDRESS_CLAMP;
        case D3D10_DDI_TEXTURE_ADDRESS_BORDER:     return SVGA3D_TEX_ADDRESS_BORDER;
        case D3D10_DDI_TEXTURE_ADDRESS_MIRRORONCE: return SVGA3D_TEX_ADDRESS_MIRRORONCE;
        default:
            break;
    }
    AssertFailed();
    return SVGA3D_TEX_ADDRESS_WRAP;
}


void vboxDXCreateSamplerState(PVBOXDX_DEVICE pDevice, PVBOXDX_SAMPLER_STATE pSamplerState)
{
    int rc = RTHandleTableAlloc(pDevice->hHTSamplerState, pSamplerState, &pSamplerState->uSamplerId);
    AssertRCReturnVoidStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    D3D10_DDI_SAMPLER_DESC *p = &pSamplerState->SamplerDesc;
    SVGA3dFilter filter                 = d3dToSvgaFilter(p->Filter);
    uint8 addressU                      = d3dToSvgaTextureAddressMode(p->AddressU);
    uint8 addressV                      = d3dToSvgaTextureAddressMode(p->AddressV);
    uint8 addressW                      = d3dToSvgaTextureAddressMode(p->AddressW);
    float mipLODBias                    = p->MipLODBias;
    uint8 maxAnisotropy                 = p->MaxAnisotropy;
    SVGA3dComparisonFunc comparisonFunc = d3dToSvgaComparisonFunc(p->ComparisonFunc);
    SVGA3dRGBAFloat borderColor;
    borderColor.value[0]                = p->BorderColor[0];
    borderColor.value[1]                = p->BorderColor[1];
    borderColor.value[2]                = p->BorderColor[2];
    borderColor.value[3]                = p->BorderColor[3];
    float minLOD                        = p->MinLOD;
    float maxLOD                        = p->MaxLOD;

    vgpu10DefineSamplerState(pDevice,
                             pSamplerState->uSamplerId,
                             filter,
                             addressU,
                             addressV,
                             addressW,
                             mipLODBias,
                             maxAnisotropy,
                             comparisonFunc,
                             borderColor,
                             minLOD,
                             maxLOD);
}


void vboxDXDestroySamplerState(PVBOXDX_DEVICE pDevice, PVBOXDX_SAMPLER_STATE pSamplerState)
{
    vgpu10DestroySamplerState(pDevice, pSamplerState->uSamplerId);
    RTHandleTableFree(pDevice->hHTSamplerState, pSamplerState->uSamplerId);
}


void vboxDXCreateElementLayout(PVBOXDX_DEVICE pDevice, PVBOXDXELEMENTLAYOUT pElementLayout)
{
    int rc = RTHandleTableAlloc(pDevice->hHTElementLayout, pElementLayout, &pElementLayout->uElementLayoutId);
    AssertRCReturnVoidStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    uint32_t const cElements = pElementLayout->NumElements;

    SVGA3dInputElementDesc *paDesc;
    if (cElements)
    {
        paDesc = (SVGA3dInputElementDesc *)RTMemTmpAlloc(cElements * sizeof(SVGA3dInputElementDesc));
        AssertReturnVoidStmt(paDesc != NULL,
            RTHandleTableFree(pDevice->hHTElementLayout, pElementLayout->uElementLayoutId);
            vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));
    }
    else
        paDesc = NULL;

    for (unsigned i = 0; i < cElements; ++i)
    {
        D3D10DDIARG_INPUT_ELEMENT_DESC const *pSrc = &pElementLayout->aVertexElements[i];
        SVGA3dInputElementDesc *pDst = &paDesc[i];
        pDst->inputSlot            = pSrc->InputSlot;
        pDst->alignedByteOffset    = pSrc->AlignedByteOffset;
        pDst->format               = vboxDXDxgiToSvgaFormat(pSrc->Format);
        pDst->inputSlotClass       = pSrc->InputSlotClass;
        pDst->instanceDataStepRate = pSrc->InstanceDataStepRate;
        pDst->inputRegister        = pSrc->InputRegister;
    }

    vgpu10DefineElementLayout(pDevice, pElementLayout->uElementLayoutId, cElements, paDesc);
    RTMemTmpFree(paDesc);
}


void vboxDXDestroyElementLayout(PVBOXDX_DEVICE pDevice, PVBOXDXELEMENTLAYOUT pElementLayout)
{
    vgpu10DestroyElementLayout(pDevice, pElementLayout->uElementLayoutId);
    RTHandleTableFree(pDevice->hHTElementLayout, pElementLayout->uElementLayoutId);
}


void vboxDXSetInputLayout(PVBOXDX_DEVICE pDevice, PVBOXDXELEMENTLAYOUT pInputLayout)
{
    uint32_t const uElementLayoutId = pInputLayout ? pInputLayout->uElementLayoutId : SVGA3D_INVALID_ID;
    vgpu10SetInputLayout(pDevice, uElementLayoutId);
}


void vboxDXSetBlendState(PVBOXDX_DEVICE pDevice, PVBOXDX_BLENDSTATE pBlendState,
                         const FLOAT BlendFactor[4], UINT SampleMask)
{
    uint32_t const uBlendId = pBlendState ? pBlendState->uBlendId : SVGA3D_INVALID_ID;
    vgpu10SetBlendState(pDevice, uBlendId, BlendFactor, SampleMask);
}


void vboxDXSetDepthStencilState(PVBOXDX_DEVICE pDevice, PVBOXDX_DEPTHSTENCIL_STATE pDepthStencilState, UINT StencilRef)
{
    uint32_t const uDepthStencilId = pDepthStencilState ? pDepthStencilState->uDepthStencilId : SVGA3D_INVALID_ID;
    vgpu10SetDepthStencilState(pDevice, uDepthStencilId, StencilRef);
}


void vboxDXSetRasterizerState(PVBOXDX_DEVICE pDevice, PVBOXDX_RASTERIZER_STATE pRasterizerState)
{
    uint32_t const uRasterizerId = pRasterizerState ? pRasterizerState->uRasterizerId : SVGA3D_INVALID_ID;
    vgpu10SetRasterizerState(pDevice, uRasterizerId);
}


void vboxDXSetSamplers(PVBOXDX_DEVICE pDevice, SVGA3dShaderType enmShaderType,
                       UINT StartSlot, UINT NumSamplers, const uint32_t *paSamplerIds)
{
    vgpu10SetSamplers(pDevice, StartSlot, enmShaderType, NumSamplers, paSamplerIds);
}


static SVGA3dPrimitiveType d3dToSvgaPrimitiveType(D3D10_DDI_PRIMITIVE_TOPOLOGY PrimitiveTopology)
{
    switch (PrimitiveTopology)
    {
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_UNDEFINED:                  return SVGA3D_PRIMITIVE_INVALID;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_POINTLIST:                  return SVGA3D_PRIMITIVE_POINTLIST;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_LINELIST:                   return SVGA3D_PRIMITIVE_LINELIST;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_LINESTRIP:                  return SVGA3D_PRIMITIVE_LINESTRIP;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLELIST:               return SVGA3D_PRIMITIVE_TRIANGLELIST;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:              return SVGA3D_PRIMITIVE_TRIANGLESTRIP;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_LINELIST_ADJ:               return SVGA3D_PRIMITIVE_LINELIST_ADJ;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ:              return SVGA3D_PRIMITIVE_LINESTRIP_ADJ;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ:           return SVGA3D_PRIMITIVE_TRIANGLELIST_ADJ;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ:          return SVGA3D_PRIMITIVE_TRIANGLESTRIP_ADJ;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST:  return SVGA3D_PRIMITIVE_1_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST:  return SVGA3D_PRIMITIVE_2_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST:  return SVGA3D_PRIMITIVE_3_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST:  return SVGA3D_PRIMITIVE_4_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_5_CONTROL_POINT_PATCHLIST:  return SVGA3D_PRIMITIVE_5_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_6_CONTROL_POINT_PATCHLIST:  return SVGA3D_PRIMITIVE_6_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_7_CONTROL_POINT_PATCHLIST:  return SVGA3D_PRIMITIVE_7_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_8_CONTROL_POINT_PATCHLIST:  return SVGA3D_PRIMITIVE_8_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_9_CONTROL_POINT_PATCHLIST:  return SVGA3D_PRIMITIVE_9_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_10_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_10_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_11_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_11_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_12_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_12_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_13_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_13_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_14_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_14_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_15_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_15_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_16_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_16_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_17_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_17_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_18_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_18_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_19_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_19_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_20_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_20_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_21_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_21_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_22_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_22_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_23_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_23_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_24_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_24_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_25_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_25_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_26_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_26_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_27_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_27_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_28_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_28_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_29_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_29_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_30_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_30_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_31_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_31_CONTROL_POINT_PATCH;
        case D3D11_DDI_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST: return SVGA3D_PRIMITIVE_32_CONTROL_POINT_PATCH;
        default:
            break;
    }
    AssertFailed();
    return SVGA3D_PRIMITIVE_INVALID;
}


void vboxDXIaSetTopology(PVBOXDX_DEVICE pDevice, D3D10_DDI_PRIMITIVE_TOPOLOGY PrimitiveTopology)
{
    SVGA3dPrimitiveType topology = d3dToSvgaPrimitiveType(PrimitiveTopology);
    vgpu10SetTopology(pDevice, topology);
}


void vboxDXDrawIndexed(PVBOXDX_DEVICE pDevice, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
{
    vboxDXSetupPipeline(pDevice);
    vgpu10DrawIndexed(pDevice, IndexCount, StartIndexLocation, BaseVertexLocation);
}


void vboxDXDraw(PVBOXDX_DEVICE pDevice, UINT VertexCount, UINT StartVertexLocation)
{
    vboxDXSetupPipeline(pDevice);
    vgpu10Draw(pDevice, VertexCount, StartVertexLocation);
}


void vboxDXDrawIndexedInstanced(PVBOXDX_DEVICE pDevice, UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation)
{
    vboxDXSetupPipeline(pDevice);
    vgpu10DrawIndexedInstanced(pDevice, IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
}


void vboxDXDrawInstanced(PVBOXDX_DEVICE pDevice, UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation)
{
    vboxDXSetupPipeline(pDevice);
    vgpu10DrawInstanced(pDevice, VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
}


void vboxDXDrawAuto(PVBOXDX_DEVICE pDevice)
{
    vboxDXSetupPipeline(pDevice);
    vgpu10DrawAuto(pDevice);
}


void vboxDXDrawIndexedInstancedIndirect(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource, UINT AlignedByteOffsetForArgs)
{
    vboxDXSetupPipeline(pDevice);
    vgpu10DrawIndexedInstancedIndirect(pDevice, vboxDXGetAllocation(pResource), AlignedByteOffsetForArgs);
}


void vboxDXDrawInstancedIndirect(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource, UINT AlignedByteOffsetForArgs)
{
    vboxDXSetupPipeline(pDevice);
    vgpu10DrawInstancedIndirect(pDevice, vboxDXGetAllocation(pResource), AlignedByteOffsetForArgs);
}


void vboxDXSetViewports(PVBOXDX_DEVICE pDevice, UINT NumViewports, UINT ClearViewports, const D3D10_DDI_VIEWPORT *pViewports)
{
    RT_NOREF(ClearViewports);
    vgpu10SetViewports(pDevice, NumViewports, pViewports);
}


void vboxDXSetScissorRects(PVBOXDX_DEVICE pDevice, UINT NumRects, UINT ClearRects, const D3D10_DDI_RECT *pRects)
{
    RT_NOREF(ClearRects);
    vgpu10SetScissorRects(pDevice, NumRects, pRects);
}


static void vboxDXDestroyCOAllocation(PVBOXDX_DEVICE pDevice, PVBOXDXCOALLOCATION pCOAllocation)
{
    if (pCOAllocation)
    {
        if (pCOAllocation->hCOAllocation)
        {
            D3DDDICB_DEALLOCATE ddiDeallocate;
            RT_ZERO(ddiDeallocate);
            ddiDeallocate.NumAllocations = 1;
            ddiDeallocate.HandleList     = &pCOAllocation->hCOAllocation;

            HRESULT hr = pDevice->pRTCallbacks->pfnDeallocateCb(pDevice->hRTDevice.handle, &ddiDeallocate);
            LogFlowFunc(("pfnDeallocateCb returned %d", hr));
            AssertStmt(SUCCEEDED(hr), vboxDXDeviceSetError(pDevice, hr));

            pCOAllocation->hCOAllocation = 0;
        }

        RTMemFree(pCOAllocation);
    }
}


static bool vboxDXCreateCOAllocation(PVBOXDX_DEVICE pDevice, RTLISTANCHOR *pList, PVBOXDXCOALLOCATION *ppCOAllocation, uint32_t cbAllocation)
{
    PVBOXDXCOALLOCATION pCOAllocation = (PVBOXDXCOALLOCATION)RTMemAllocZ(sizeof(VBOXDXCOALLOCATION));
    AssertReturnStmt(pCOAllocation, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY), false);

    VBOXDXALLOCATIONDESC desc;
    RT_ZERO(desc);
    desc.enmAllocationType = VBOXDXALLOCATIONTYPE_CO;
    desc.cbAllocation      = cbAllocation;

    D3DDDI_ALLOCATIONINFO2 ddiAllocationInfo;
    RT_ZERO(ddiAllocationInfo);
    ddiAllocationInfo.pPrivateDriverData    = &desc;
    ddiAllocationInfo.PrivateDriverDataSize = sizeof(desc);

    D3DDDICB_ALLOCATE ddiAllocate;
    RT_ZERO(ddiAllocate);
    ddiAllocate.NumAllocations   = 1;
    ddiAllocate.pAllocationInfo2 = &ddiAllocationInfo;

    HRESULT hr = pDevice->pRTCallbacks->pfnAllocateCb(pDevice->hRTDevice.handle, &ddiAllocate);
    LogFlowFunc(("pfnAllocateCb returned %d, hKMResource 0x%X, hAllocation 0x%X", hr, ddiAllocate.hKMResource, ddiAllocationInfo.hAllocation));
    AssertReturnStmt(SUCCEEDED(hr),
                     vboxDXDestroyCOAllocation(pDevice, pCOAllocation); vboxDXDeviceSetError(pDevice, hr), false);

    pCOAllocation->hCOAllocation = ddiAllocationInfo.hAllocation;

    D3DDDICB_LOCK ddiLock;
    RT_ZERO(ddiLock);
    ddiLock.hAllocation = ddiAllocationInfo.hAllocation;
    ddiLock.Flags.WriteOnly = 1;
    hr = pDevice->pRTCallbacks->pfnLockCb(pDevice->hRTDevice.handle, &ddiLock);
    if (SUCCEEDED(hr))
    {
        memset(ddiLock.pData, 0, cbAllocation);

        D3DDDICB_UNLOCK ddiUnlock;
        ddiUnlock.NumAllocations = 1;
        ddiUnlock.phAllocations = &ddiAllocationInfo.hAllocation;
        hr = pDevice->pRTCallbacks->pfnUnlockCb(pDevice->hRTDevice.handle, &ddiUnlock);
    }
    AssertReturnStmt(SUCCEEDED(hr),
                     vboxDXDestroyCOAllocation(pDevice, pCOAllocation); vboxDXDeviceSetError(pDevice, hr), false);

    pCOAllocation->cbAllocation = cbAllocation;

    /* Initially the allocation contains one big free block and zero sized free blocks. */
    pCOAllocation->aOffset[0] = 0;
    for (unsigned i = 1; i < RT_ELEMENTS(pCOAllocation->aOffset); ++i)
        pCOAllocation->aOffset[i] = cbAllocation;

    RTListAppend(pList, &pCOAllocation->nodeAllocationsChain);

    *ppCOAllocation = pCOAllocation;
    return true;
}

#define IS_CO_BLOCK_FREE(_a, _i) (((_a)->u64Bitmap & (1ULL << (_i))) == 0)
#define IS_CO_BLOCK_USED(_a, _i) (((_a)->u64Bitmap & (1ULL << (_i))) != 0)
#define SET_CO_BLOCK_FREE(_a, _i) (((_a)->u64Bitmap &= ~(1ULL << (_i))))
#define SET_CO_BLOCK_USED(_a, _i) (((_a)->u64Bitmap |= (1ULL << (_i))))

static bool vboxDXCOABlockAlloc(PVBOXDXCOALLOCATION pCOAllocation, uint32_t cb, uint32_t *poff)
{
    //DEBUG_BREAKPOINT_TEST();
    /* Search for a big enough free block. The last block is a special case. */
    unsigned i = 0;
    for (; i < RT_ELEMENTS(pCOAllocation->aOffset) - 1; ++i)
    {
        if (   IS_CO_BLOCK_FREE(pCOAllocation, i)
            && pCOAllocation->aOffset[i + 1] - pCOAllocation->aOffset[i] >= cb)
        {
            /* Found one. */
            SET_CO_BLOCK_USED(pCOAllocation, i);

            /* If the next block is free, then add the remaining space to it. */
            if (IS_CO_BLOCK_FREE(pCOAllocation, i + 1))
                pCOAllocation->aOffset[i + 1] = pCOAllocation->aOffset[i] + cb;

            *poff = pCOAllocation->aOffset[i];
            return true;
        }
    }

    /* Last block. */
    if (   IS_CO_BLOCK_FREE(pCOAllocation, i)
        && pCOAllocation->cbAllocation - pCOAllocation->aOffset[i] >= cb)
    {
        /* Found one. */
        SET_CO_BLOCK_USED(pCOAllocation, i);

        *poff = pCOAllocation->aOffset[i];
        return true;
    }

    return false;
}


static void vboxDXCOABlockFree(PVBOXDXCOALLOCATION pCOAllocation, uint32_t offBlock)
{
    //DEBUG_BREAKPOINT_TEST();
    for (unsigned i = 0; i < RT_ELEMENTS(pCOAllocation->aOffset); ++i)
    {
        if (pCOAllocation->aOffset[i] == offBlock)
        {
            Assert(IS_CO_BLOCK_USED(pCOAllocation, i));
            SET_CO_BLOCK_FREE(pCOAllocation, i);
            return;
        }
    }

    AssertFailed();
}


static bool vboxDXEnsureShaderAllocation(PVBOXDX_DEVICE pDevice)
{
    if (!pDevice->hShaderAllocation)
    {
        VBOXDXALLOCATIONDESC desc;
        RT_ZERO(desc);
        desc.enmAllocationType = VBOXDXALLOCATIONTYPE_SHADERS;
        desc.cbAllocation      = SVGA3D_MAX_SHADER_MEMORY_BYTES;

        D3DDDI_ALLOCATIONINFO2 ddiAllocationInfo;
        RT_ZERO(ddiAllocationInfo);
        //ddiAllocationInfo.pSystemMem            = 0;
        ddiAllocationInfo.pPrivateDriverData    = &desc;
        ddiAllocationInfo.PrivateDriverDataSize = sizeof(desc);
        //ddiAllocationInfo.VidPnSourceId         = 0;
        //ddiAllocationInfo.Flags.Value           = 0;

        D3DDDICB_ALLOCATE ddiAllocate;
        RT_ZERO(ddiAllocate);
        //ddiAllocate.pPrivateDriverData    = NULL;
        //ddiAllocate.PrivateDriverDataSize = 0;
        //ddiAllocate.hResource             = 0;
        ddiAllocate.NumAllocations        = 1;
        ddiAllocate.pAllocationInfo2      = &ddiAllocationInfo;

        HRESULT hr = pDevice->pRTCallbacks->pfnAllocateCb(pDevice->hRTDevice.handle, &ddiAllocate);
        LogFlowFunc((" pfnAllocateCb returned %d, hKMResource 0x%X, hAllocation 0x%X", hr, ddiAllocate.hKMResource, ddiAllocationInfo.hAllocation));
        AssertReturnStmt(SUCCEEDED(hr), vboxDXDeviceSetError(pDevice, hr), false);

        pDevice->hShaderAllocation  = ddiAllocationInfo.hAllocation;
        pDevice->cbShaderAllocation = SVGA3D_MAX_SHADER_MEMORY_BYTES;
        pDevice->cbShaderAvailable  = SVGA3D_MAX_SHADER_MEMORY_BYTES;
        pDevice->offShaderFree      = 0;
    }

    return true;
}


static SVGA3dDXSignatureSemanticName d3dToSvgaSemanticName(D3D10_SB_NAME SystemValue)
{
    /** @todo */
    return (SVGA3dDXSignatureSemanticName)SystemValue;
}


static SVGA3dDXSignatureRegisterComponentType d3dToSvgaComponentType(D3D10_SB_REGISTER_COMPONENT_TYPE RegisterComponentType)
{
    /** @todo */
    return (SVGA3dDXSignatureRegisterComponentType)RegisterComponentType;
}


static SVGA3dDXSignatureMinPrecision d3dToSvgaMinPrecision(D3D11_SB_OPERAND_MIN_PRECISION MinPrecision)
{
    /** @todo */
    return (SVGA3dDXSignatureMinPrecision)MinPrecision;
}


void vboxDXCreateShader(PVBOXDX_DEVICE pDevice, SVGA3dShaderType enmShaderType, PVBOXDXSHADER pShader, const UINT* pShaderCode,
                        const D3D11_1DDIARG_SIGNATURE_ENTRY2* pInputSignature, UINT NumInputSignatureEntries,
                        const D3D11_1DDIARG_SIGNATURE_ENTRY2* pOutputSignature, UINT NumOutputSignatureEntries,
                        const D3D11_1DDIARG_SIGNATURE_ENTRY2* pPatchConstantSignature, UINT NumPatchConstantSignatureEntries)
{
    /* CreateGeometryShaderWithStreamOutput sometimes passes pShaderCode == NULL. */
    pShader->enmShaderType = enmShaderType;
    pShader->cbShader = pShaderCode ? pShaderCode[1] * sizeof(UINT) : 0;
    pShader->cbSignatures = sizeof(SVGA3dDXSignatureHeader)
       + NumInputSignatureEntries * sizeof(SVGA3dDXShaderSignatureEntry)
       + NumOutputSignatureEntries * sizeof(SVGA3dDXShaderSignatureEntry)
       + NumPatchConstantSignatureEntries * sizeof(SVGA3dDXShaderSignatureEntry);

    if (pShader->enmShaderType == SVGA3D_SHADERTYPE_GS)
    {
        RT_ZERO(pShader->gs);
        pShader->gs.uStreamOutputId = SVGA3D_INVALID_ID;
        pShader->gs.offStreamOutputDecls = SVGA3D_INVALID_ID;
    }

    if (!pShaderCode)
    {
        RT_ZERO(pShader->node);
        pShader->uShaderId = SVGA3D_INVALID_ID;
        pShader->offShader = SVGA3D_INVALID_ID;
        pShader->pu32Bytecode = NULL;
        pShader->pSignatures = NULL;
        return;
    }

    int rc = RTHandleTableAlloc(pDevice->hHTShader, pShader, &pShader->uShaderId);
    AssertRCReturnVoidStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    if (!vboxDXEnsureShaderAllocation(pDevice))
    {
        RTHandleTableFree(pDevice->hHTShader, pShader->uShaderId);
        return;
    }

    uint32_t const cbShaderTotal = pShader->cbShader + pShader->cbSignatures;
    if (pDevice->cbShaderAllocation - pDevice->offShaderFree < cbShaderTotal)
    {
        if (pDevice->cbShaderAvailable < cbShaderTotal)
        {
            /** @todo Unbind some shaders until there is enough space for the new shader. */
            DEBUG_BREAKPOINT_TEST();
        }
        Assert(pDevice->cbShaderAvailable < cbShaderTotal);

        /** @todo Pack shaders in order to have one free area in the end of the allocation. */
        DEBUG_BREAKPOINT_TEST();
    }

    pShader->offShader = pDevice->offShaderFree;
    pShader->pu32Bytecode = (uint32_t *)&pShader[1];
    pShader->pSignatures = (SVGA3dDXSignatureHeader *)((uint8_t *)pShader->pu32Bytecode + pShader->cbShader);

    memcpy(pShader->pu32Bytecode, pShaderCode, pShader->cbShader);

    pShader->pSignatures->headerVersion = SVGADX_SIGNATURE_HEADER_VERSION_0;
    pShader->pSignatures->numInputSignatures = NumInputSignatureEntries;
    pShader->pSignatures->numOutputSignatures = NumOutputSignatureEntries;
    pShader->pSignatures->numPatchConstantSignatures = NumPatchConstantSignatureEntries;

    SVGA3dDXShaderSignatureEntry *pSignatureEntry = (SVGA3dDXShaderSignatureEntry *)&pShader->pSignatures[1];
    for (unsigned i = 0; i < NumInputSignatureEntries; ++i, ++pSignatureEntry)
    {
        pSignatureEntry->registerIndex = pInputSignature[i].Register;
        pSignatureEntry->semanticName  = d3dToSvgaSemanticName(pInputSignature[i].SystemValue);
        pSignatureEntry->mask          = pInputSignature[i].Mask;
        pSignatureEntry->componentType = d3dToSvgaComponentType(pInputSignature[i].RegisterComponentType);
        pSignatureEntry->minPrecision  = d3dToSvgaMinPrecision(pInputSignature[i].MinPrecision);
    }
    for (unsigned i = 0; i < NumOutputSignatureEntries; ++i, ++pSignatureEntry)
    {
        pSignatureEntry->registerIndex = pOutputSignature[i].Register;
        pSignatureEntry->semanticName  = d3dToSvgaSemanticName(pOutputSignature[i].SystemValue);
        pSignatureEntry->mask          = pOutputSignature[i].Mask;
        pSignatureEntry->componentType = d3dToSvgaComponentType(pOutputSignature[i].RegisterComponentType);
        pSignatureEntry->minPrecision  = d3dToSvgaMinPrecision(pOutputSignature[i].MinPrecision);
    }
    for (unsigned i = 0; i < NumPatchConstantSignatureEntries; ++i, ++pSignatureEntry)
    {
        pSignatureEntry->registerIndex = pPatchConstantSignature[i].Register;
        pSignatureEntry->semanticName  = d3dToSvgaSemanticName(pPatchConstantSignature[i].SystemValue);
        pSignatureEntry->mask          = pPatchConstantSignature[i].Mask;
        pSignatureEntry->componentType = d3dToSvgaComponentType(pPatchConstantSignature[i].RegisterComponentType);
        pSignatureEntry->minPrecision  = d3dToSvgaMinPrecision(pPatchConstantSignature[i].MinPrecision);
    }

    D3DDDICB_LOCK ddiLock;
    RT_ZERO(ddiLock);
    ddiLock.hAllocation = pDevice->hShaderAllocation;
    ddiLock.Flags.WriteOnly = 1;
    HRESULT hr = pDevice->pRTCallbacks->pfnLockCb(pDevice->hRTDevice.handle, &ddiLock);
    if (SUCCEEDED(hr))
    {
        uint8_t *pu8 = (uint8_t *)ddiLock.pData + pShader->offShader;

        memcpy(pu8, pShader->pu32Bytecode, pShader->cbShader);
        pu8 += pShader->cbShader;

        memcpy(pu8, pShader->pSignatures, pShader->cbSignatures);

        D3DDDICB_UNLOCK ddiUnlock;
        ddiUnlock.NumAllocations = 1;
        ddiUnlock.phAllocations = &pDevice->hShaderAllocation;
        hr = pDevice->pRTCallbacks->pfnUnlockCb(pDevice->hRTDevice.handle, &ddiUnlock);
    }
    AssertReturnVoidStmt(SUCCEEDED(hr),
                         RTHandleTableFree(pDevice->hHTShader, pShader->uShaderId);
                         vboxDXDeviceSetError(pDevice, hr));

    RTListAppend(&pDevice->listShaders, &pShader->node);

    pDevice->cbShaderAvailable -= cbShaderTotal;
    pDevice->offShaderFree     += cbShaderTotal;

    vgpu10DefineShader(pDevice, pShader->uShaderId, pShader->enmShaderType, cbShaderTotal);
    vgpu10BindShader(pDevice, pShader->uShaderId, pDevice->hShaderAllocation, pShader->offShader);
}


static void vboxDXHandleFree(RTHANDLETABLE hHT, uint32_t *pu32Id)
{
    RTHandleTableFree(hHT, *pu32Id);
    *pu32Id = SVGA3D_INVALID_ID;
}


void vboxDXCreateStreamOutput(PVBOXDX_DEVICE pDevice, PVBOXDXSHADER pShader,
                              const D3D11DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY *pOutputStreamDecl, UINT NumEntries,
                              const UINT  *BufferStridesInBytes, UINT NumStrides,
                              UINT RasterizedStream)
{
    AssertReturnVoidStmt(NumEntries <= SVGA3D_MAX_STREAMOUT_DECLS, vboxDXDeviceSetError(pDevice, E_INVALIDARG));

    pShader->gs.NumEntries       = NumEntries;
    pShader->gs.NumStrides       = RT_MIN(NumStrides, SVGA3D_DX_MAX_SOTARGETS);
    memcpy(pShader->gs.BufferStridesInBytes, BufferStridesInBytes, pShader->gs.NumStrides * sizeof(UINT));
    pShader->gs.RasterizedStream = RasterizedStream;

    int rc = RTHandleTableAlloc(pDevice->hHTStreamOutput, pShader, &pShader->gs.uStreamOutputId);
    AssertRCReturnVoidStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    /* Allocate mob space for declarations. */
    pShader->gs.cbOutputStreamDecls = pShader->gs.NumEntries * sizeof(*pOutputStreamDecl);
    pShader->gs.pCOAllocation = NULL;
    PVBOXDXCOALLOCATION pIter;
    RTListForEach(&pDevice->listCOAStreamOutput, pIter, VBOXDXCOALLOCATION, nodeAllocationsChain)
    {
        if (vboxDXCOABlockAlloc(pIter, pShader->gs.cbOutputStreamDecls, &pShader->gs.offStreamOutputDecls))
        {
            pShader->gs.pCOAllocation = pIter;
            break;
        }
    }

    if (!pShader->gs.pCOAllocation)
    {
        /* Create a new allocation.  */
        if (!vboxDXCreateCOAllocation(pDevice, &pDevice->listCOAStreamOutput, &pShader->gs.pCOAllocation, 8 * pShader->gs.cbOutputStreamDecls))
            AssertFailedReturnVoidStmt(vboxDXHandleFree(pDevice->hHTStreamOutput, &pShader->gs.uStreamOutputId);
                                       vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

        if (!vboxDXCOABlockAlloc(pShader->gs.pCOAllocation, pShader->gs.cbOutputStreamDecls, &pShader->gs.offStreamOutputDecls))
            AssertFailedReturnVoidStmt(vboxDXHandleFree(pDevice->hHTStreamOutput, &pShader->gs.uStreamOutputId);
                                       vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));
    }

    D3DDDICB_LOCK ddiLock;
    RT_ZERO(ddiLock);
    ddiLock.hAllocation = pShader->gs.pCOAllocation->hCOAllocation;
    ddiLock.Flags.WriteOnly = 1;
    HRESULT hr = pDevice->pRTCallbacks->pfnLockCb(pDevice->hRTDevice.handle, &ddiLock);
    if (SUCCEEDED(hr))
    {
        uint8_t *pu8 = (uint8_t *)ddiLock.pData + pShader->gs.offStreamOutputDecls;

        D3D11DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY const *src = pOutputStreamDecl;
        SVGA3dStreamOutputDeclarationEntry *dst = (SVGA3dStreamOutputDeclarationEntry *)pu8;
        for (unsigned i = 0; i < pShader->gs.NumEntries; ++i)
        {
            dst->outputSlot    = src->OutputSlot;
            dst->registerIndex = src->RegisterIndex;
            dst->registerMask  = src->RegisterMask;
            dst->pad0          = 0;
            dst->pad1          = 0;
            dst->stream        = src->Stream;
            ++dst;
            ++src;
        }

        D3DDDICB_UNLOCK ddiUnlock;
        ddiUnlock.NumAllocations = 1;
        ddiUnlock.phAllocations = &pShader->gs.pCOAllocation->hCOAllocation;
        hr = pDevice->pRTCallbacks->pfnUnlockCb(pDevice->hRTDevice.handle, &ddiUnlock);
    }
    AssertReturnVoidStmt(SUCCEEDED(hr),
                         vboxDXHandleFree(pDevice->hHTShader, &pShader->uShaderId);
                         vboxDXDeviceSetError(pDevice, hr));

    /* Inform host. */
    vgpu10DefineStreamOutputWithMob(pDevice, pShader->gs.uStreamOutputId, pShader->gs.NumEntries, pShader->gs.NumStrides,
                                    pShader->gs.BufferStridesInBytes, pShader->gs.RasterizedStream);
    vgpu10BindStreamOutput(pDevice, pShader->gs.uStreamOutputId, pShader->gs.pCOAllocation->hCOAllocation,
                           pShader->gs.offStreamOutputDecls, pShader->gs.cbOutputStreamDecls);
}


void vboxDXDestroyShader(PVBOXDX_DEVICE pDevice, PVBOXDXSHADER pShader)
{
    if (pShader->enmShaderType == SVGA3D_SHADERTYPE_GS)
    {
        if (pShader->gs.offStreamOutputDecls != SVGA3D_INVALID_ID)
        {
            vboxDXCOABlockFree(pShader->gs.pCOAllocation, pShader->gs.offStreamOutputDecls);
            pShader->gs.offStreamOutputDecls = SVGA3D_INVALID_ID;
            pShader->gs.pCOAllocation = NULL;
        }

        if (pShader->gs.uStreamOutputId != SVGA3D_INVALID_ID)
            vboxDXHandleFree(pDevice->hHTStreamOutput, &pShader->gs.uStreamOutputId);
    }

    if (pShader->uShaderId != SVGA3D_INVALID_ID)
    {
        /* Send VGPU commands. */
        vgpu10BindShader(pDevice, pShader->uShaderId, 0, 0);
        vgpu10DestroyShader(pDevice, pShader->uShaderId);
        vboxDXDeviceFlushCommands(pDevice);

        /* Take the freed space into account. */
        uint32_t const cbShaderTotal = pShader->cbShader + pShader->cbSignatures;
        pDevice->cbShaderAvailable += cbShaderTotal;

        RTListNodeRemove(&pShader->node);
        RTHandleTableFree(pDevice->hHTShader, pShader->uShaderId);
    }
}

typedef struct VMSVGAQUERYINFO
{
    D3D10DDI_QUERY  queryTypeDDI;
    uint32_t        cbDataDDI;
    SVGA3dQueryType queryTypeSvga;
    uint32_t        cbDataSvga;
} VMSVGAQUERYINFO;

static VMSVGAQUERYINFO const *getQueryInfo(D3D10DDI_QUERY Query)
{
    static VMSVGAQUERYINFO const aQueryInfo[D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM3 + 1] =
    {
        { D3D10DDI_QUERY_EVENT,                           sizeof(BOOL),
            SVGA3D_QUERYTYPE_INVALID,                        sizeof(UINT64) },
        { D3D10DDI_QUERY_OCCLUSION,                       sizeof(UINT64),
            SVGA3D_QUERYTYPE_OCCLUSION64,                    sizeof(SVGADXOcclusion64QueryResult) },
        { D3D10DDI_QUERY_TIMESTAMP,                       sizeof(UINT64),
            SVGA3D_QUERYTYPE_TIMESTAMP,                      sizeof(SVGADXTimestampQueryResult) },
        { D3D10DDI_QUERY_TIMESTAMPDISJOINT,               sizeof(D3D10_DDI_QUERY_DATA_TIMESTAMP_DISJOINT),
            SVGA3D_QUERYTYPE_TIMESTAMPDISJOINT,              sizeof(SVGADXTimestampDisjointQueryResult) },
        { D3D10DDI_QUERY_PIPELINESTATS,                   sizeof(D3D10_DDI_QUERY_DATA_PIPELINE_STATISTICS),
            SVGA3D_QUERYTYPE_PIPELINESTATS,                  sizeof(SVGADXPipelineStatisticsQueryResult) },
        { D3D10DDI_QUERY_OCCLUSIONPREDICATE,              sizeof(BOOL),
            SVGA3D_QUERYTYPE_OCCLUSIONPREDICATE,             sizeof(SVGADXOcclusionPredicateQueryResult) },
        { D3D10DDI_QUERY_STREAMOUTPUTSTATS,               sizeof(D3D10_DDI_QUERY_DATA_SO_STATISTICS),
            SVGA3D_QUERYTYPE_STREAMOUTPUTSTATS,              sizeof(SVGADXStreamOutStatisticsQueryResult) },
        { D3D10DDI_QUERY_STREAMOVERFLOWPREDICATE,         sizeof(BOOL),
            SVGA3D_QUERYTYPE_STREAMOVERFLOWPREDICATE,        sizeof(SVGADXStreamOutPredicateQueryResult) },
        { D3D11DDI_QUERY_PIPELINESTATS,                   sizeof(D3D11_DDI_QUERY_DATA_PIPELINE_STATISTICS),
            SVGA3D_QUERYTYPE_PIPELINESTATS,                  sizeof(SVGADXPipelineStatisticsQueryResult) },
        { D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM0,       sizeof(D3D10_DDI_QUERY_DATA_SO_STATISTICS),
            SVGA3D_QUERYTYPE_SOSTATS_STREAM0,                sizeof(SVGADXStreamOutStatisticsQueryResult) },
        { D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM1,       sizeof(D3D10_DDI_QUERY_DATA_SO_STATISTICS),
            SVGA3D_QUERYTYPE_SOSTATS_STREAM1,                sizeof(SVGADXStreamOutStatisticsQueryResult) },
        { D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM2,       sizeof(D3D10_DDI_QUERY_DATA_SO_STATISTICS),
            SVGA3D_QUERYTYPE_SOSTATS_STREAM2,                sizeof(SVGADXStreamOutStatisticsQueryResult) },
        { D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM3,       sizeof(D3D10_DDI_QUERY_DATA_SO_STATISTICS),
            SVGA3D_QUERYTYPE_SOSTATS_STREAM3,                sizeof(SVGADXStreamOutStatisticsQueryResult) },
        { D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM0, sizeof(BOOL),
            SVGA3D_QUERYTYPE_SOP_STREAM0,                    sizeof(SVGADXStreamOutPredicateQueryResult) },
        { D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM1, sizeof(BOOL),
            SVGA3D_QUERYTYPE_SOP_STREAM1,                    sizeof(SVGADXStreamOutPredicateQueryResult) },
        { D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM2, sizeof(BOOL),
            SVGA3D_QUERYTYPE_SOP_STREAM2,                    sizeof(SVGADXStreamOutPredicateQueryResult) },
        { D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM3, sizeof(BOOL),
            SVGA3D_QUERYTYPE_SOP_STREAM3,                     sizeof(SVGADXStreamOutPredicateQueryResult) },
    };

    AssertReturn(Query < RT_ELEMENTS(aQueryInfo), NULL);
    return &aQueryInfo[Query];
}


#ifdef DEBUG
static bool isBeginDisabled(D3D10DDI_QUERY q)
{
    return q == D3D10DDI_QUERY_EVENT
        || q == D3D10DDI_QUERY_TIMESTAMP;
}
#endif


void vboxDXCreateQuery(PVBOXDX_DEVICE pDevice, PVBOXDXQUERY pQuery, D3D10DDI_QUERY Query, UINT MiscFlags)
{
    VMSVGAQUERYINFO const *pQueryInfo = getQueryInfo(Query);
    AssertReturnVoidStmt(pQueryInfo, vboxDXDeviceSetError(pDevice, E_INVALIDARG));

    pQuery->Query = Query;
    pQuery->svga.queryType = pQueryInfo->queryTypeSvga;
    pQuery->svga.flags = 0;
    if (MiscFlags & D3D10DDI_QUERY_MISCFLAG_PREDICATEHINT)
        pQuery->svga.flags |= SVGA3D_DXQUERY_FLAG_PREDICATEHINT;
    pQuery->enmQueryState = VBOXDXQUERYSTATE_CREATED;
    pQuery->u64Value = 0;

    int rc = RTHandleTableAlloc(pDevice->hHTQuery, pQuery, &pQuery->uQueryId);
    AssertRCReturnVoidStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    /* Allocate mob space for this query. */
    pQuery->pCOAllocation = NULL;
    uint32_t const cbAlloc = (pQuery->Query != D3D10DDI_QUERY_EVENT ? sizeof(uint32_t) : 0) + pQueryInfo->cbDataSvga;
    PVBOXDXCOALLOCATION pIter;
    RTListForEach(&pDevice->listCOAQuery, pIter, VBOXDXCOALLOCATION, nodeAllocationsChain)
    {
        if (vboxDXCOABlockAlloc(pIter, cbAlloc, &pQuery->offQuery))
        {
            pQuery->pCOAllocation = pIter;
            break;
        }
    }

    if (!pQuery->pCOAllocation)
    {
        /* Create a new allocation.  */
        if (!vboxDXCreateCOAllocation(pDevice, &pDevice->listCOAQuery, &pQuery->pCOAllocation, 4 * _1K))
            AssertFailedReturnVoidStmt(RTHandleTableFree(pDevice->hHTQuery, pQuery->uQueryId);
                                       vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

        if (!vboxDXCOABlockAlloc(pQuery->pCOAllocation, cbAlloc, &pQuery->offQuery))
            AssertFailedReturnVoidStmt(RTHandleTableFree(pDevice->hHTQuery, pQuery->uQueryId);
                                       vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));
    }

    RTListAppend(&pDevice->listQueries, &pQuery->nodeQuery);

    if (pQuery->Query != D3D10DDI_QUERY_EVENT)
    {
        D3DDDICB_LOCK ddiLock;
        RT_ZERO(ddiLock);
        ddiLock.hAllocation = pQuery->pCOAllocation->hCOAllocation;
        ddiLock.Flags.WriteOnly = 1;
        HRESULT hr = pDevice->pRTCallbacks->pfnLockCb(pDevice->hRTDevice.handle, &ddiLock);
        if (SUCCEEDED(hr))
        {
            *(uint32_t *)((uint8_t *)ddiLock.pData + pQuery->offQuery) = SVGA3D_QUERYSTATE_PENDING;

            D3DDDICB_UNLOCK ddiUnlock;
            ddiUnlock.NumAllocations = 1;
            ddiUnlock.phAllocations = &pQuery->pCOAllocation->hCOAllocation;
            hr = pDevice->pRTCallbacks->pfnUnlockCb(pDevice->hRTDevice.handle, &ddiUnlock);
        }
        AssertReturnVoidStmt(SUCCEEDED(hr), vboxDXDeviceSetError(pDevice, hr));

        vgpu10DefineQuery(pDevice, pQuery->uQueryId, pQuery->svga.queryType, pQuery->svga.flags);
        vgpu10BindQuery(pDevice, pQuery->uQueryId, pQuery->pCOAllocation->hCOAllocation);
        vgpu10SetQueryOffset(pDevice, pQuery->uQueryId, pQuery->offQuery);
    }
}


void vboxDXDestroyQuery(PVBOXDX_DEVICE pDevice, PVBOXDXQUERY pQuery)
{
    if (pQuery->Query != D3D10DDI_QUERY_EVENT)
        vgpu10DestroyQuery(pDevice, pQuery->uQueryId);

    if (pQuery->pCOAllocation)
    {
        vboxDXCOABlockFree(pQuery->pCOAllocation, pQuery->offQuery);
        pQuery->pCOAllocation = NULL;
    }

    RTListNodeRemove(&pQuery->nodeQuery);
    RTHandleTableFree(pDevice->hHTQuery, pQuery->uQueryId);
}


void vboxDXQueryBegin(PVBOXDX_DEVICE pDevice, PVBOXDXQUERY pQuery)
{
    Assert(pQuery->enmQueryState == VBOXDXQUERYSTATE_CREATED || pQuery->enmQueryState == VBOXDXQUERYSTATE_SIGNALED);

    pQuery->enmQueryState = VBOXDXQUERYSTATE_BUILDING;
    if (pQuery->Query == D3D10DDI_QUERY_EVENT)
        return;

    vgpu10BeginQuery(pDevice, pQuery->uQueryId);
}


void vboxDXQueryEnd(PVBOXDX_DEVICE pDevice, PVBOXDXQUERY pQuery)
{
    Assert(   pQuery->enmQueryState == VBOXDXQUERYSTATE_BUILDING
           || (   isBeginDisabled(pQuery->Query)
               && (   pQuery->enmQueryState == VBOXDXQUERYSTATE_CREATED
                   || pQuery->enmQueryState == VBOXDXQUERYSTATE_SIGNALED)
              )
          );

    pQuery->enmQueryState = VBOXDXQUERYSTATE_ISSUED;

    if (pQuery->Query == D3D10DDI_QUERY_EVENT)
    {
        pQuery->u64Value = ASMAtomicIncU64(&pDevice->u64MobFenceValue);
        vgpu10MobFence64(pDevice, pQuery->u64Value, pQuery->pCOAllocation->hCOAllocation, pQuery->offQuery);
        return;
    }

    vgpu10EndQuery(pDevice, pQuery->uQueryId);
}


void vboxDXQueryGetData(PVBOXDX_DEVICE pDevice, PVBOXDXQUERY pQuery, VOID* pData, UINT DataSize, UINT Flags)
{
    Assert(pQuery->enmQueryState == VBOXDXQUERYSTATE_ISSUED || pQuery->enmQueryState == VBOXDXQUERYSTATE_SIGNALED);

    if (!RT_BOOL(Flags & D3D10_DDI_GET_DATA_DO_NOT_FLUSH))
        vboxDXDeviceFlushCommands(pDevice);

    if (pQuery->Query == D3D10DDI_QUERY_EVENT)
    {
        uint64_t u64Value = 0;

        D3DDDICB_LOCK ddiLock;
        RT_ZERO(ddiLock);
        ddiLock.hAllocation = pQuery->pCOAllocation->hCOAllocation;
        ddiLock.Flags.ReadOnly = 1;
        HRESULT hr = pDevice->pRTCallbacks->pfnLockCb(pDevice->hRTDevice.handle, &ddiLock);
        if (SUCCEEDED(hr))
        {
            u64Value = *(uint64_t *)((uint8_t *)ddiLock.pData + pQuery->offQuery);

            D3DDDICB_UNLOCK ddiUnlock;
            ddiUnlock.NumAllocations = 1;
            ddiUnlock.phAllocations = &pQuery->pCOAllocation->hCOAllocation;
            hr = pDevice->pRTCallbacks->pfnUnlockCb(pDevice->hRTDevice.handle, &ddiUnlock);
        }
        AssertReturnVoidStmt(SUCCEEDED(hr), vboxDXDeviceSetError(pDevice, hr));

        if (u64Value < pQuery->u64Value)
            vboxDXDeviceSetError(pDevice, DXGI_DDI_ERR_WASSTILLDRAWING);
        else
        {
            pQuery->enmQueryState = VBOXDXQUERYSTATE_SIGNALED;

            if (pData && DataSize >= sizeof(BOOL))
                *(BOOL *)pData = TRUE;
        }
        return;
    }

    vgpu10ReadbackQuery(pDevice, pQuery->uQueryId);

    VMSVGAQUERYINFO const *pQueryInfo = getQueryInfo(pQuery->Query);
    AssertReturnVoidStmt(pQueryInfo, vboxDXDeviceSetError(pDevice, E_INVALIDARG));

    void *pvResult = RTMemTmpAlloc(pQueryInfo->cbDataSvga);
    AssertReturnVoidStmt(pvResult, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    uint32_t u32QueryStatus = SVGA3D_QUERYSTATE_PENDING;

    D3DDDICB_LOCK ddiLock;
    RT_ZERO(ddiLock);
    ddiLock.hAllocation = pQuery->pCOAllocation->hCOAllocation;
    ddiLock.Flags.ReadOnly = 1;
    HRESULT hr = pDevice->pRTCallbacks->pfnLockCb(pDevice->hRTDevice.handle, &ddiLock);
    if (SUCCEEDED(hr))
    {
        uint8_t *pu8 = (uint8_t *)ddiLock.pData + pQuery->offQuery;
        u32QueryStatus = *(uint32_t *)pu8;

        memcpy(pvResult, pu8 + sizeof(uint32_t), pQueryInfo->cbDataSvga);

        D3DDDICB_UNLOCK ddiUnlock;
        ddiUnlock.NumAllocations = 1;
        ddiUnlock.phAllocations = &pQuery->pCOAllocation->hCOAllocation;
        hr = pDevice->pRTCallbacks->pfnUnlockCb(pDevice->hRTDevice.handle, &ddiUnlock);
    }
    AssertReturnVoidStmt(SUCCEEDED(hr), RTMemTmpFree(pvResult); vboxDXDeviceSetError(pDevice, hr));

    if (u32QueryStatus != SVGA3D_QUERYSTATE_SUCCEEDED)
        vboxDXDeviceSetError(pDevice, DXGI_DDI_ERR_WASSTILLDRAWING);
    else
    {
        pQuery->enmQueryState = VBOXDXQUERYSTATE_SIGNALED;

        if (pData && DataSize >= pQueryInfo->cbDataDDI)
        {
            typedef union DDIQUERYRESULT
            {
                UINT64                                   occlusion;            /* D3D10DDI_QUERY_OCCLUSION */
                UINT64                                   timestamp;            /* D3D10DDI_QUERY_TIMESTAMP */
                D3D10_DDI_QUERY_DATA_TIMESTAMP_DISJOINT  timestampDisjoint;    /* D3D10DDI_QUERY_TIMESTAMPDISJOINT */
                D3D10_DDI_QUERY_DATA_PIPELINE_STATISTICS pipelineStatistics10; /* D3D10DDI_QUERY_PIPELINESTATS */
                BOOL                                     occlusionPredicate;   /* D3D10DDI_QUERY_OCCLUSIONPREDICATE */
                D3D10_DDI_QUERY_DATA_SO_STATISTICS       soStatistics;         /* D3D10DDI_QUERY_STREAMOUTPUTSTATS, D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM[0-3] */
                BOOL                                     soOverflowPredicate;  /* D3D10DDI_QUERY_STREAMOVERFLOWPREDICATE, D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM[0-3] */
                D3D11_DDI_QUERY_DATA_PIPELINE_STATISTICS pipelineStatistics11; /* D3D11DDI_QUERY_PIPELINESTATS */
            } DDIQUERYRESULT;
            SVGADXQueryResultUnion const *pSvgaData = (SVGADXQueryResultUnion *)pvResult;
            DDIQUERYRESULT *pDDIData = (DDIQUERYRESULT *)pData;
            switch (pQuery->Query)
            {
                case D3D10DDI_QUERY_OCCLUSION:
                {
                    pDDIData->occlusion = pSvgaData->occ.samplesRendered;
                    break;
                }
                case D3D10DDI_QUERY_TIMESTAMP:
                {
                    pDDIData->timestamp = pSvgaData->ts.timestamp;
                    break;
                }
                case D3D10DDI_QUERY_TIMESTAMPDISJOINT:
                {
                    pDDIData->timestampDisjoint.Frequency = pSvgaData->tsDisjoint.realFrequency;
                    pDDIData->timestampDisjoint.Disjoint = pSvgaData->tsDisjoint.disjoint;
                    break;
                }
                case D3D10DDI_QUERY_PIPELINESTATS:
                {
                    pDDIData->pipelineStatistics10.IAVertices    = pSvgaData->pipelineStats.inputAssemblyVertices;
                    pDDIData->pipelineStatistics10.IAPrimitives  = pSvgaData->pipelineStats.inputAssemblyPrimitives;
                    pDDIData->pipelineStatistics10.VSInvocations = pSvgaData->pipelineStats.vertexShaderInvocations;
                    pDDIData->pipelineStatistics10.GSInvocations = pSvgaData->pipelineStats.geometryShaderInvocations;
                    pDDIData->pipelineStatistics10.GSPrimitives  = pSvgaData->pipelineStats.geometryShaderPrimitives;
                    pDDIData->pipelineStatistics10.CInvocations  = pSvgaData->pipelineStats.clipperInvocations;
                    pDDIData->pipelineStatistics10.CPrimitives   = pSvgaData->pipelineStats.clipperPrimitives;
                    pDDIData->pipelineStatistics10.PSInvocations = pSvgaData->pipelineStats.pixelShaderInvocations;
                    break;
                }
                case D3D10DDI_QUERY_OCCLUSIONPREDICATE:
                {
                    pDDIData->occlusionPredicate = pSvgaData->occPred.anySamplesRendered;
                    break;
                }
                case D3D10DDI_QUERY_STREAMOUTPUTSTATS:
                case D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM0:
                case D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM1:
                case D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM2:
                case D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM3:
                {
                    pDDIData->soStatistics.NumPrimitivesWritten    = pSvgaData->soStats.numPrimitivesWritten;
                    pDDIData->soStatistics.PrimitivesStorageNeeded = pSvgaData->soStats.numPrimitivesRequired;
                    break;
                }
                case D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM0:
                case D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM1:
                case D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM2:
                case D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM3:
                case D3D10DDI_QUERY_STREAMOVERFLOWPREDICATE:
                {
                    pDDIData->soOverflowPredicate = pSvgaData->soPred.overflowed;
                    break;
                }
                case D3D11DDI_QUERY_PIPELINESTATS:
                {
                    pDDIData->pipelineStatistics11.IAVertices    = pSvgaData->pipelineStats.inputAssemblyVertices;
                    pDDIData->pipelineStatistics11.IAPrimitives  = pSvgaData->pipelineStats.inputAssemblyPrimitives;
                    pDDIData->pipelineStatistics11.VSInvocations = pSvgaData->pipelineStats.vertexShaderInvocations;
                    pDDIData->pipelineStatistics11.GSInvocations = pSvgaData->pipelineStats.geometryShaderInvocations;
                    pDDIData->pipelineStatistics11.GSPrimitives  = pSvgaData->pipelineStats.geometryShaderPrimitives;
                    pDDIData->pipelineStatistics11.CInvocations  = pSvgaData->pipelineStats.clipperInvocations;
                    pDDIData->pipelineStatistics11.CPrimitives   = pSvgaData->pipelineStats.clipperPrimitives;
                    pDDIData->pipelineStatistics11.PSInvocations = pSvgaData->pipelineStats.pixelShaderInvocations;
                    pDDIData->pipelineStatistics11.HSInvocations = pSvgaData->pipelineStats.hullShaderInvocations;
                    pDDIData->pipelineStatistics11.DSInvocations = pSvgaData->pipelineStats.domainShaderInvocations;
                    pDDIData->pipelineStatistics11.CSInvocations = pSvgaData->pipelineStats.computeShaderInvocations;
                    break;
                }
                default:
                    break;
            }
        }
    }

    RTMemTmpFree(pvResult);
}


void vboxDXSetPredication(PVBOXDX_DEVICE pDevice, PVBOXDXQUERY pQuery, BOOL PredicateValue)
{
    vgpu10SetPredication(pDevice, pQuery ? pQuery->uQueryId : SVGA3D_INVALID_ID, PredicateValue);
}


void vboxDXSetShader(PVBOXDX_DEVICE pDevice, SVGA3dShaderType enmShaderType, PVBOXDXSHADER pShader)
{
    if (enmShaderType == SVGA3D_SHADERTYPE_GS)
        vgpu10SetStreamOutput(pDevice, pShader ? pShader->gs.uStreamOutputId : SVGA3D_INVALID_ID);
    vgpu10SetShader(pDevice, pShader ? pShader->uShaderId : SVGA3D_INVALID_ID, enmShaderType);
}


void vboxDXSetVertexBuffers(PVBOXDX_DEVICE pDevice, UINT StartSlot, UINT NumBuffers,
                            PVBOXDX_RESOURCE *papBuffers, const UINT *pStrides, const UINT *pOffsets)
{
    AssertReturnVoidStmt(  StartSlot < SVGA3D_MAX_VERTEX_ARRAYS
                         && NumBuffers <= SVGA3D_MAX_VERTEX_ARRAYS
                         && StartSlot + NumBuffers <= SVGA3D_MAX_VERTEX_ARRAYS,
                         vboxDXDeviceSetError(pDevice, E_INVALIDARG));

    /* Remember which buffers must be set. The buffers will be actually set right before a draw call,
     * because this allows the updates of the buffers content to be done prior to setting the buffers on the host.
     */
    PVBOXDXVERTEXBUFFERSSTATE pVBS = &pDevice->pipeline.VertexBuffers;

    for (unsigned i = 0; i < NumBuffers; ++i)
    {
        pVBS->apResource[StartSlot + i] = papBuffers[i];
        pVBS->aStrides[StartSlot + i] = pStrides[i];
        pVBS->aOffsets[StartSlot + i] = pOffsets[i];
        LogFunc(("slot %d, stride %d, offset %d",
                 StartSlot + i, pVBS->aStrides[StartSlot + i], pVBS->aOffsets[StartSlot + i]));
    }

    /* Join the current range and the new range. */
    if (pVBS->NumBuffers == 0)
    {
        pVBS->StartSlot = StartSlot;
        pVBS->NumBuffers = NumBuffers;
    }
    else
    {
        UINT FirstSlot = RT_MIN(StartSlot, pVBS->StartSlot);
        UINT EndSlot = RT_MAX(pVBS->StartSlot + pVBS->NumBuffers, StartSlot + NumBuffers);
        pVBS->StartSlot = FirstSlot;
        pVBS->NumBuffers = EndSlot - FirstSlot;
    }
}


void vboxDXSetIndexBuffer(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pBuffer, DXGI_FORMAT Format, UINT Offset)
{
    PVBOXDXINDEXBUFFERSTATE pIBS = &pDevice->pipeline.IndexBuffer;
    pIBS->pBuffer = pBuffer;
    pIBS->Format = Format;
    pIBS->Offset = Offset;
}


void vboxDXSoSetTargets(PVBOXDX_DEVICE pDevice, uint32_t NumTargets,
                        D3DKMT_HANDLE *paAllocations, uint32_t *paOffsets, uint32_t *paSizes)
{
    vgpu10SoSetTargets(pDevice, NumTargets, paAllocations, paOffsets, paSizes);
}


static bool vboxDXDynamicOrStagingUpdateUP(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pDstResource,
                                           UINT DstSubresource, const D3D10_DDI_BOX *pDstBox,
                                           const VOID *pSysMemUP, UINT RowPitch, UINT DepthPitch, UINT CopyFlags)
{
    RT_NOREF(CopyFlags);
    AssertReturnStmt(   pDstResource->Usage == D3D10_DDI_USAGE_DYNAMIC
                     || pDstResource->Usage == D3D10_DDI_USAGE_STAGING,
                     vboxDXDeviceSetError(pDevice, E_INVALIDARG), false);

    SVGA3dBox destBox;
    if (pDstBox)
    {
        destBox.x = pDstBox->left;
        destBox.y = pDstBox->top;
        destBox.z = pDstBox->front;
        destBox.w = pDstBox->right - pDstBox->left;
        destBox.h = pDstBox->bottom - pDstBox->top;
        destBox.d = pDstBox->back - pDstBox->front;
    }
    else
    {
        vboxDXGetSubresourceBox(pDstResource, DstSubresource, &destBox);
    }

    uint32_t offPixel;
    uint32_t cbRow;
    uint32_t cRows;
    uint32_t Depth;
    vboxDXGetResourceBoxDimensions(pDstResource, DstSubresource, &destBox, &offPixel, &cbRow, &cRows, &Depth);

    UINT DstRowPitch;
    UINT DstDepthPitch;
    vboxDXGetSubresourcePitch(pDstResource, DstSubresource, &DstRowPitch, &DstDepthPitch);

    /* The allocation contains all subresources, so get subresource offset too. */
    offPixel += vboxDXGetSubresourceOffset(pDstResource, DstSubresource);
    //uint32_t const cbSubresource = vboxDXGetSubresourceSize(pDstResource, DstSubresource);

    D3DDDICB_LOCK ddiLock;
    RT_ZERO(ddiLock);
    ddiLock.hAllocation = vboxDXGetAllocation(pDstResource);
    ddiLock.Flags.WriteOnly = 1;
    HRESULT hr = pDevice->pRTCallbacks->pfnLockCb(pDevice->hRTDevice.handle, &ddiLock);
    if (SUCCEEDED(hr))
    {
        for (unsigned z = 0; z < Depth; ++z)
        {
            uint8_t *pu8Dst = (uint8_t *)ddiLock.pData + offPixel + z * DstDepthPitch;
            uint8_t const *pu8Src = (uint8_t *)pSysMemUP + z * DepthPitch;
            for (unsigned y = 0; y < cRows; ++y)
            {
                memcpy(pu8Dst, pu8Src, cbRow);
                pu8Dst += DstRowPitch;
                pu8Src += RowPitch;
            }
        }

        D3DKMT_HANDLE const hAllocation = vboxDXGetAllocation(pDstResource);

        D3DDDICB_UNLOCK ddiUnlock;
        ddiUnlock.NumAllocations = 1;
        ddiUnlock.phAllocations = &hAllocation;
        hr = pDevice->pRTCallbacks->pfnUnlockCb(pDevice->hRTDevice.handle, &ddiUnlock);
        if (SUCCEEDED(hr))
        {
            /* Inform the host that the resource has been updated. */
            SVGA3dBox box;
            vboxDXGetSubresourceBox(pDstResource, DstSubresource, &box);
            vgpu10UpdateSubResource(pDevice, vboxDXGetAllocation(pDstResource), DstSubresource, &box);
            return true;
        }
    }
    vboxDXDeviceSetError(pDevice, hr);
    return false;
}


static bool vboxDXUpdateStagingBufferUP(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pBuffer,
                                        UINT offDstPixel, UINT cbRow, UINT cRows, UINT DstRowPitch, UINT Depth, UINT DstDepthPitch,
                                        const VOID *pSysMemUP, UINT SrcRowPitch, UINT SrcDepthPitch)
{
    D3DDDICB_LOCK ddiLock;
    RT_ZERO(ddiLock);
    ddiLock.hAllocation = vboxDXGetAllocation(pBuffer);
    ddiLock.Flags.WriteOnly = 1;
    HRESULT hr = pDevice->pRTCallbacks->pfnLockCb(pDevice->hRTDevice.handle, &ddiLock);
    if (SUCCEEDED(hr))
    {
        /* Placement of the data in the destination buffer is the same as in the surface. */
        for (unsigned z = 0; z < Depth; ++z)
        {
            uint8_t *pu8Dst = (uint8_t *)ddiLock.pData + offDstPixel + z * DstDepthPitch;
            uint8_t const *pu8Src = (uint8_t *)pSysMemUP + z * SrcDepthPitch;
            for (unsigned y = 0; y < cRows; ++y)
            {
                memcpy(pu8Dst, pu8Src, cbRow);
                pu8Dst += DstRowPitch;
                pu8Src += SrcRowPitch;
            }
        }

        D3DKMT_HANDLE const hAllocation = vboxDXGetAllocation(pBuffer);

        D3DDDICB_UNLOCK ddiUnlock;
        ddiUnlock.NumAllocations = 1;
        ddiUnlock.phAllocations = &hAllocation;
        hr = pDevice->pRTCallbacks->pfnUnlockCb(pDevice->hRTDevice.handle, &ddiUnlock);
        if (SUCCEEDED(hr))
            return true;
    }
    vboxDXDeviceSetError(pDevice, hr);
    return false;
}


static PVBOXDX_RESOURCE vboxDXCreateStagingBuffer(PVBOXDX_DEVICE pDevice, UINT cbAllocation)
{
    PVBOXDX_RESOURCE pStagingResource = (PVBOXDX_RESOURCE)RTMemAlloc(sizeof(VBOXDX_RESOURCE)
                                                                     + 1 * sizeof(D3D10DDI_MIPINFO));
    AssertReturnStmt(pStagingResource, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY), false);

    D3D11DDIARG_CREATERESOURCE createResource;
    D3D10DDI_MIPINFO mipInfo;

    mipInfo.TexelWidth     = cbAllocation;
    mipInfo.TexelHeight    = 1;
    mipInfo.TexelDepth     = 1;
    mipInfo.PhysicalWidth  = mipInfo.TexelWidth;
    mipInfo.PhysicalHeight = mipInfo.TexelHeight;
    mipInfo.PhysicalDepth  = mipInfo.TexelDepth;

    createResource.pMipInfoList       = &mipInfo;
    createResource.pInitialDataUP     = 0;
    createResource.ResourceDimension  = D3D10DDIRESOURCE_BUFFER;
    createResource.Usage              = D3D10_DDI_USAGE_STAGING;
    createResource.BindFlags          = 0;
    createResource.MapFlags           = D3D10_DDI_CPU_ACCESS_WRITE;
    createResource.MiscFlags          = 0;
    createResource.Format             = DXGI_FORMAT_UNKNOWN;
    createResource.SampleDesc.Count   = 0;
    createResource.SampleDesc.Quality = 0;
    createResource.MipLevels          = 1;
    createResource.ArraySize          = 1;
    createResource.pPrimaryDesc       = 0;
    createResource.ByteStride         = 0;
    createResource.DecoderBufferType  = D3D11_1DDI_VIDEO_DECODER_BUFFER_UNKNOWN;
    createResource.TextureLayout      = D3DWDDM2_0DDI_TL_UNDEFINED;

    pStagingResource->hRTResource.handle = 0; /* This resource has not been created by D3D runtime. */
    int rc = vboxDXInitResourceData(pStagingResource, &createResource);
    if (RT_SUCCESS(rc))
    {
        if (vboxDXCreateResource(pDevice, pStagingResource, &createResource))
            return pStagingResource;
    }
    RTMemFree(pStagingResource);
    vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY);
    return NULL;
}


static HRESULT dxReclaimStagingAllocation(PVBOXDX_DEVICE pDevice, PVBOXDXKMRESOURCE pStagingKMResource)
{
    BOOL fDiscarded = FALSE;
    D3DDDICB_RECLAIMALLOCATIONS ddiReclaimAllocations;
    RT_ZERO(ddiReclaimAllocations);
    ddiReclaimAllocations.pResources = NULL;
    ddiReclaimAllocations.HandleList = &pStagingKMResource->hAllocation;
    ddiReclaimAllocations.pDiscarded = &fDiscarded;
    ddiReclaimAllocations.NumAllocations = 1;

    HRESULT hr = pDevice->pRTCallbacks->pfnReclaimAllocationsCb(pDevice->hRTDevice.handle, &ddiReclaimAllocations);
    LogFlowFunc(("pfnReclaimAllocationsCb returned %d, fDiscarded %d", hr, fDiscarded));
    Assert(SUCCEEDED(hr));
    return hr;
}


static HRESULT dxOfferStagingAllocation(PVBOXDX_DEVICE pDevice, PVBOXDXKMRESOURCE pStagingKMResource)
{
    D3DDDICB_OFFERALLOCATIONS ddiOfferAllocations;
    RT_ZERO(ddiOfferAllocations);
    ddiOfferAllocations.pResources = NULL;
    ddiOfferAllocations.HandleList = &pStagingKMResource->hAllocation;
    ddiOfferAllocations.NumAllocations = 1;
    ddiOfferAllocations.Priority = D3DDDI_OFFER_PRIORITY_LOW;

    HRESULT hr = pDevice->pRTCallbacks->pfnOfferAllocationsCb(pDevice->hRTDevice.handle, &ddiOfferAllocations);
    LogFlowFunc(("pfnOfferAllocationsCb returned %d", hr));
    Assert(SUCCEEDED(hr));
    return hr;
}


void vboxDXResourceUpdateSubresourceUP(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pDstResource,
                                       UINT DstSubresource, const D3D10_DDI_BOX *pDstBox,
                                       const VOID *pSysMemUP, UINT RowPitch, UINT DepthPitch, UINT CopyFlags)
{
    if (   pDstResource->Usage != D3D10_DDI_USAGE_DEFAULT
        && pDstResource->Usage != D3D10_DDI_USAGE_IMMUTABLE)
    {
        vboxDXDynamicOrStagingUpdateUP(pDevice, pDstResource, DstSubresource, pDstBox,
                                       pSysMemUP, RowPitch, DepthPitch, CopyFlags);
        return;
    }

    /* DEFAULT resources are updated via a staging buffer. */

    /*
     * A simple approach for now: allocate a staging buffer for each upload and delete the buffers after a flush.
     */

    /*
     * Allocate a staging buffer big enough to hold the entire subresource.
     */
    uint32_t const cbStagingBuffer = vboxDXGetSubresourceSize(pDstResource, DstSubresource);
    PVBOXDX_RESOURCE pStagingBuffer = vboxDXCreateStagingBuffer(pDevice, cbStagingBuffer);
    if (!pStagingBuffer)
        return;

    /*
     * Copy data to staging via map/unmap.
     */
    SVGA3dBox destBox;
    if (pDstBox)
    {
        destBox.x = pDstBox->left;
        destBox.y = pDstBox->top;
        destBox.z = pDstBox->front;
        destBox.w = pDstBox->right - pDstBox->left;
        destBox.h = pDstBox->bottom - pDstBox->top;
        destBox.d = pDstBox->back - pDstBox->front;
    }
    else
        vboxDXGetSubresourceBox(pDstResource, DstSubresource, &destBox);

    uint32_t offPixel;
    uint32_t cbRow;
    UINT cRows;
    UINT Depth;
    vboxDXGetResourceBoxDimensions(pDstResource, DstSubresource, &destBox, &offPixel, &cbRow, &cRows, &Depth);

    UINT cbRowPitch;
    UINT cbDepthPitch;
    vboxDXGetSubresourcePitch(pDstResource, DstSubresource, &cbRowPitch, &cbDepthPitch);

    if (!vboxDXUpdateStagingBufferUP(pDevice, pStagingBuffer,
                                     offPixel, cbRow, cRows, cbRowPitch, Depth, cbDepthPitch,
                                     pSysMemUP, RowPitch, DepthPitch))
        return;

    /*
     * Copy from staging to destination.
     */
    /* Inform the host that the staging buffer has been updated. Part occupied by the DstSubresource. */
    SVGA3dBox box;
    box.x = 0;
    box.y = 0;
    box.z = 0;
    box.w = cbStagingBuffer;
    box.h = 1;
    box.d = 1;
    vgpu10UpdateSubResource(pDevice, vboxDXGetAllocation(pStagingBuffer), 0, &box);

    /* Issue SVGA_3D_CMD_DX_TRANSFER_FROM_BUFFER */
    uint32 srcOffset = offPixel;
    uint32 srcPitch = cbRowPitch;
    uint32 srcSlicePitch = cbDepthPitch;
    vgpu10TransferFromBuffer(pDevice, vboxDXGetAllocation(pStagingBuffer), srcOffset, srcPitch, srcSlicePitch,
                             vboxDXGetAllocation(pDstResource), DstSubresource, destBox);

    RTListPrepend(&pDevice->listStagingResources, &pStagingBuffer->pKMResource->nodeStaging);
}

#ifndef D3DERR_WASSTILLDRAWING
#define D3DERR_WASSTILLDRAWING 0x8876021c
#endif

void vboxDXResourceMap(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource, UINT Subresource,
                       D3D10_DDI_MAP DDIMap, UINT Flags, D3D10DDI_MAPPED_SUBRESOURCE *pMappedSubResource)
{
    /** @todo Need to take into account various variants Dynamic/Staging/ Discard/NoOverwrite, etc. */
    Assert(pResource->uMap == 0); /* Must not be already mapped */

    if (dxIsAllocationInUse(pDevice, vboxDXGetAllocation(pResource)))
    {
        vboxDXFlush(pDevice, true);

        if (RT_BOOL(Flags & D3D10_DDI_MAP_FLAG_DONOTWAIT))
        {
            vboxDXDeviceSetError(pDevice, DXGI_DDI_ERR_WASSTILLDRAWING);
            return;
        }
    }

    /* Readback for read access. */
    if (DDIMap == D3D10_DDI_MAP_READ || DDIMap == D3D10_DDI_MAP_READWRITE)
    {
        vgpu10ReadbackSubResource(pDevice, vboxDXGetAllocation(pResource), Subresource);
        vboxDXFlush(pDevice, true);
        /* DXGK now knows that the allocation is in use. So pfnLockCb waits until the data is ready. */
    }

    HRESULT hr;
    D3DDDICB_LOCK ddiLock;
    do
    {
        RT_ZERO(ddiLock);
        ddiLock.hAllocation = vboxDXGetAllocation(pResource);
        ddiLock.Flags.ReadOnly =   DDIMap == D3D10_DDI_MAP_READ;
        ddiLock.Flags.WriteOnly =  DDIMap == D3D10_DDI_MAP_WRITE
                                || DDIMap == D3D10_DDI_MAP_WRITE_DISCARD
                                || DDIMap == D3D10_DDI_MAP_WRITE_NOOVERWRITE;
        ddiLock.Flags.DonotWait = RT_BOOL(Flags & D3D10_DDI_MAP_FLAG_DONOTWAIT);
        /// @todo ddiLock.Flags.Discard = DDIMap == D3D10_DDI_MAP_WRITE_DISCARD;
        /** @todo Other flags? */

        hr = pDevice->pRTCallbacks->pfnLockCb(pDevice->hRTDevice.handle, &ddiLock);
        if (hr == D3DERR_WASSTILLDRAWING)
        {
            if (RT_BOOL(Flags & D3D10_DDI_MAP_FLAG_DONOTWAIT))
            {
                vboxDXDeviceSetError(pDevice, DXGI_DDI_ERR_WASSTILLDRAWING);
                return;
            }

            RTThreadYield();
        }
    } while (hr == D3DERR_WASSTILLDRAWING);

    if (SUCCEEDED(hr))
    {
        /* "If the Discard bit-field flag is set in the Flags member, the video memory manager creates
         * a new instance of the allocation and returns a new handle that represents the new instance."
         */
        if (DDIMap == D3D10_DDI_MAP_WRITE_DISCARD)
            pResource->pKMResource->hAllocation = ddiLock.hAllocation;

        uint32_t const offSubresource = vboxDXGetSubresourceOffset(pResource, Subresource);
        pMappedSubResource->pData = (uint8_t *)ddiLock.pData + offSubresource;
        vboxDXGetSubresourcePitch(pResource, Subresource, &pMappedSubResource->RowPitch, &pMappedSubResource->DepthPitch);

        pResource->DDIMap = DDIMap;
    }
    else
        vboxDXDeviceSetError(pDevice, hr);
}


void vboxDXResourceUnmap(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource, UINT Subresource)
{
    D3DKMT_HANDLE const hAllocation = vboxDXGetAllocation(pResource);

    D3DDDICB_UNLOCK ddiUnlock;
    ddiUnlock.NumAllocations = 1;
    ddiUnlock.phAllocations = &hAllocation;
    HRESULT hr = pDevice->pRTCallbacks->pfnUnlockCb(pDevice->hRTDevice.handle, &ddiUnlock);
    if (SUCCEEDED(hr))
    {
        if (   pResource->DDIMap == D3D10_DDI_MAP_WRITE
            || pResource->DDIMap == D3D10_DDI_MAP_WRITE_DISCARD
            || pResource->DDIMap == D3D10_DDI_MAP_WRITE_NOOVERWRITE)
        {
            /* Inform the host that the resource has been updated. */
            SVGA3dBox box;
            vboxDXGetSubresourceBox(pResource, Subresource, &box);
            vgpu10UpdateSubResource(pDevice, vboxDXGetAllocation(pResource), Subresource, &box);
        }

        pResource->uMap = 0;
    }
    else
        vboxDXDeviceSetError(pDevice, hr);
}


static SVGA3dResourceType d3dToSvgaResourceDimension(D3D10DDIRESOURCE_TYPE ResourceDimension)
{
    switch (ResourceDimension)
    {
        case D3D10DDIRESOURCE_BUFFER:      return SVGA3D_RESOURCE_BUFFER;
        case D3D10DDIRESOURCE_TEXTURE1D:   return SVGA3D_RESOURCE_TEXTURE1D;
        case D3D10DDIRESOURCE_TEXTURE2D:   return SVGA3D_RESOURCE_TEXTURE2D;
        case D3D10DDIRESOURCE_TEXTURE3D:   return SVGA3D_RESOURCE_TEXTURE3D;
        case D3D10DDIRESOURCE_TEXTURECUBE: return SVGA3D_RESOURCE_TEXTURECUBE;
        case D3D11DDIRESOURCE_BUFFEREX:    return SVGA3D_RESOURCE_BUFFEREX;
    }
    AssertFailed();
    return D3D10DDIRESOURCE_BUFFER;
}


void vboxDXCreateShaderResourceView(PVBOXDX_DEVICE pDevice, PVBOXDXSHADERRESOURCEVIEW pShaderResourceView)
{
    int rc = RTHandleTableAlloc(pDevice->hHTShaderResourceView, pShaderResourceView, &pShaderResourceView->uShaderResourceViewId);
    AssertRCReturnVoidStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    pShaderResourceView->svga.format            = vboxDXDxgiToSvgaFormat(pShaderResourceView->Format);
    pShaderResourceView->svga.resourceDimension = d3dToSvgaResourceDimension(pShaderResourceView->ResourceDimension);
    SVGA3dShaderResourceViewDesc *pDesc         = &pShaderResourceView->svga.desc;
    RT_ZERO(*pDesc);
    switch (pShaderResourceView->ResourceDimension)
    {
        case D3D10DDIRESOURCE_BUFFER:
            pDesc->buffer.firstElement   = pShaderResourceView->DimensionDesc.Buffer.FirstElement;
            pDesc->buffer.numElements    = pShaderResourceView->DimensionDesc.Buffer.NumElements;
            break;
        case D3D10DDIRESOURCE_TEXTURE1D:
            pDesc->tex.mostDetailedMip   = pShaderResourceView->DimensionDesc.Tex1D.MostDetailedMip;
            pDesc->tex.firstArraySlice   = pShaderResourceView->DimensionDesc.Tex1D.FirstArraySlice;
            pDesc->tex.mipLevels         = pShaderResourceView->DimensionDesc.Tex1D.MipLevels;
            pDesc->tex.arraySize         = pShaderResourceView->DimensionDesc.Tex1D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURE2D:
            pDesc->tex.mostDetailedMip   = pShaderResourceView->DimensionDesc.Tex2D.MostDetailedMip;
            pDesc->tex.firstArraySlice   = pShaderResourceView->DimensionDesc.Tex2D.FirstArraySlice;
            pDesc->tex.mipLevels         = pShaderResourceView->DimensionDesc.Tex2D.MipLevels;
            pDesc->tex.arraySize         = pShaderResourceView->DimensionDesc.Tex2D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURE3D:
            pDesc->tex.mostDetailedMip   = pShaderResourceView->DimensionDesc.Tex3D.MostDetailedMip;
            pDesc->tex.firstArraySlice   = 0;
            pDesc->tex.mipLevels         = pShaderResourceView->DimensionDesc.Tex3D.MipLevels;
            pDesc->tex.arraySize         = 0;
            break;
        case D3D10DDIRESOURCE_TEXTURECUBE:
            pDesc->tex.mostDetailedMip   = pShaderResourceView->DimensionDesc.TexCube.MostDetailedMip;
            pDesc->tex.firstArraySlice   = pShaderResourceView->DimensionDesc.TexCube.First2DArrayFace;
            pDesc->tex.mipLevels         = pShaderResourceView->DimensionDesc.TexCube.MipLevels;
            pDesc->tex.arraySize         = pShaderResourceView->DimensionDesc.TexCube.NumCubes;
            break;
        case D3D11DDIRESOURCE_BUFFEREX:
            pDesc->bufferex.firstElement = pShaderResourceView->DimensionDesc.BufferEx.FirstElement;;
            pDesc->bufferex.numElements  = pShaderResourceView->DimensionDesc.BufferEx.NumElements;
            pDesc->bufferex.flags        = pShaderResourceView->DimensionDesc.BufferEx.Flags;
            break;
        default:
            vboxDXDeviceSetError(pDevice, E_INVALIDARG);
            RTHandleTableFree(pDevice->hHTShaderResourceView, pShaderResourceView->uShaderResourceViewId);
            return;
    }

    vgpu10DefineShaderResourceView(pDevice, pShaderResourceView->uShaderResourceViewId, vboxDXGetAllocation(pShaderResourceView->pResource),
                                   pShaderResourceView->svga.format, pShaderResourceView->svga.resourceDimension,
                                   &pShaderResourceView->svga.desc);

    pShaderResourceView->fDefined = true;
    RTListAppend(&pShaderResourceView->pResource->listSRV, &pShaderResourceView->nodeView);
}


void vboxDXGenMips(PVBOXDX_DEVICE pDevice, PVBOXDXSHADERRESOURCEVIEW pShaderResourceView)
{
    vgpu10GenMips(pDevice, pShaderResourceView->uShaderResourceViewId);
}


void vboxDXDestroyShaderResourceView(PVBOXDX_DEVICE pDevice, PVBOXDXSHADERRESOURCEVIEW pShaderResourceView)
{
    RTListNodeRemove(&pShaderResourceView->nodeView);

    vgpu10DestroyShaderResourceView(pDevice, pShaderResourceView->uShaderResourceViewId);
    RTHandleTableFree(pDevice->hHTShaderResourceView, pShaderResourceView->uShaderResourceViewId);
}


void vboxDXCreateRenderTargetView(PVBOXDX_DEVICE pDevice, PVBOXDXRENDERTARGETVIEW pRenderTargetView)
{
    int rc = RTHandleTableAlloc(pDevice->hHTRenderTargetView, pRenderTargetView, &pRenderTargetView->uRenderTargetViewId);
    AssertRCReturnVoidStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    pRenderTargetView->svga.format            = vboxDXDxgiToSvgaFormat(pRenderTargetView->Format);
    pRenderTargetView->svga.resourceDimension = d3dToSvgaResourceDimension(pRenderTargetView->ResourceDimension);
    SVGA3dRenderTargetViewDesc *pDesc         = &pRenderTargetView->svga.desc;
    RT_ZERO(*pDesc);
    switch (pRenderTargetView->ResourceDimension)
    {
        case D3D10DDIRESOURCE_BUFFER:
            pDesc->buffer.firstElement = pRenderTargetView->DimensionDesc.Buffer.FirstElement;
            pDesc->buffer.numElements  = pRenderTargetView->DimensionDesc.Buffer.NumElements;
            break;
        case D3D10DDIRESOURCE_TEXTURE1D:
            pDesc->tex.mipSlice        = pRenderTargetView->DimensionDesc.Tex1D.MipSlice;
            pDesc->tex.firstArraySlice = pRenderTargetView->DimensionDesc.Tex1D.FirstArraySlice;
            pDesc->tex.arraySize       = pRenderTargetView->DimensionDesc.Tex1D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURE2D:
            pDesc->tex.mipSlice        = pRenderTargetView->DimensionDesc.Tex2D.MipSlice;
            pDesc->tex.firstArraySlice = pRenderTargetView->DimensionDesc.Tex2D.FirstArraySlice;
            pDesc->tex.arraySize       = pRenderTargetView->DimensionDesc.Tex2D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURE3D:
            pDesc->tex3D.mipSlice      = pRenderTargetView->DimensionDesc.Tex3D.MipSlice;
            pDesc->tex3D.firstW        = pRenderTargetView->DimensionDesc.Tex3D.FirstW;
            pDesc->tex3D.wSize         = pRenderTargetView->DimensionDesc.Tex3D.WSize;
            break;
        case D3D10DDIRESOURCE_TEXTURECUBE:
            pDesc->tex.mipSlice        = pRenderTargetView->DimensionDesc.TexCube.MipSlice;
            pDesc->tex.firstArraySlice = pRenderTargetView->DimensionDesc.TexCube.FirstArraySlice;
            pDesc->tex.arraySize       = pRenderTargetView->DimensionDesc.TexCube.ArraySize;
            break;
        default:
            vboxDXDeviceSetError(pDevice, E_INVALIDARG);
            RTHandleTableFree(pDevice->hHTRenderTargetView, pRenderTargetView->uRenderTargetViewId);
            return;
    }

    vgpu10DefineRenderTargetView(pDevice, pRenderTargetView->uRenderTargetViewId, vboxDXGetAllocation(pRenderTargetView->pResource),
                                 pRenderTargetView->svga.format, pRenderTargetView->svga.resourceDimension,
                                 &pRenderTargetView->svga.desc);

    pRenderTargetView->fDefined = true;
    RTListAppend(&pRenderTargetView->pResource->listRTV, &pRenderTargetView->nodeView);
}


void vboxDXClearRenderTargetView(PVBOXDX_DEVICE pDevice, PVBOXDXRENDERTARGETVIEW pRenderTargetView, const FLOAT ColorRGBA[4])
{
    vgpu10ClearRenderTargetView(pDevice, pRenderTargetView->uRenderTargetViewId, ColorRGBA);
}


void vboxDXClearRenderTargetViewRegion(PVBOXDX_DEVICE pDevice, PVBOXDXRENDERTARGETVIEW pRenderTargetView, const FLOAT Color[4], const D3D10_DDI_RECT *pRect, UINT NumRects)
{
    vgpu10ClearRenderTargetViewRegion(pDevice, pRenderTargetView->uRenderTargetViewId, Color, pRect, NumRects);
}


void vboxDXDestroyRenderTargetView(PVBOXDX_DEVICE pDevice, PVBOXDXRENDERTARGETVIEW pRenderTargetView)
{
    for (unsigned i = 0; i < pDevice->pipeline.cRenderTargetViews; ++i)
    {
        if (pDevice->pipeline.apRenderTargetViews[i] == pRenderTargetView)
        {
            DEBUG_BREAKPOINT_TEST();
        }
    }

    RTListNodeRemove(&pRenderTargetView->nodeView);

    vgpu10DestroyRenderTargetView(pDevice, pRenderTargetView->uRenderTargetViewId);
    RTHandleTableFree(pDevice->hHTRenderTargetView, pRenderTargetView->uRenderTargetViewId);
}


void vboxDXCreateDepthStencilView(PVBOXDX_DEVICE pDevice, PVBOXDXDEPTHSTENCILVIEW pDepthStencilView)
{
    int rc = RTHandleTableAlloc(pDevice->hHTDepthStencilView, pDepthStencilView, &pDepthStencilView->uDepthStencilViewId);
    AssertRCReturnVoidStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    pDepthStencilView->svga.format           = vboxDXDxgiToSvgaFormat(pDepthStencilView->Format);
    pDepthStencilView->svga.resourceDimension = d3dToSvgaResourceDimension(pDepthStencilView->ResourceDimension);
    switch (pDepthStencilView->ResourceDimension)
    {
        case D3D10DDIRESOURCE_TEXTURE1D:
            pDepthStencilView->svga.mipSlice        = pDepthStencilView->DimensionDesc.Tex1D.MipSlice;
            pDepthStencilView->svga.firstArraySlice = pDepthStencilView->DimensionDesc.Tex1D.FirstArraySlice;
            pDepthStencilView->svga.arraySize       = pDepthStencilView->DimensionDesc.Tex1D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURE2D:
            pDepthStencilView->svga.mipSlice        = pDepthStencilView->DimensionDesc.Tex2D.MipSlice;
            pDepthStencilView->svga.firstArraySlice = pDepthStencilView->DimensionDesc.Tex2D.FirstArraySlice;
            pDepthStencilView->svga.arraySize       = pDepthStencilView->DimensionDesc.Tex2D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURECUBE:
            pDepthStencilView->svga.mipSlice        = pDepthStencilView->DimensionDesc.TexCube.MipSlice;
            pDepthStencilView->svga.firstArraySlice = pDepthStencilView->DimensionDesc.TexCube.FirstArraySlice;
            pDepthStencilView->svga.arraySize       = pDepthStencilView->DimensionDesc.TexCube.ArraySize;
            break;
        default:
            vboxDXDeviceSetError(pDevice, E_INVALIDARG);
            RTHandleTableFree(pDevice->hHTDepthStencilView, pDepthStencilView->uDepthStencilViewId);
            return;
    }
    pDepthStencilView->svga.flags = pDepthStencilView->Flags;

    vgpu10DefineDepthStencilView(pDevice, pDepthStencilView->uDepthStencilViewId, vboxDXGetAllocation(pDepthStencilView->pResource),
                                 pDepthStencilView->svga.format, pDepthStencilView->svga.resourceDimension,
                                 pDepthStencilView->svga.mipSlice, pDepthStencilView->svga.firstArraySlice,
                                 pDepthStencilView->svga.arraySize, pDepthStencilView->svga.flags);

    pDepthStencilView->fDefined = true;
    RTListAppend(&pDepthStencilView->pResource->listRTV, &pDepthStencilView->nodeView);
}


void vboxDXClearDepthStencilView(PVBOXDX_DEVICE pDevice, PVBOXDXDEPTHSTENCILVIEW pDepthStencilView,
                                 UINT Flags, FLOAT Depth, UINT8 Stencil)
{
    uint16_t svgaFlags = 0;
    if (Flags & D3D10_DDI_CLEAR_DEPTH)
        svgaFlags |= SVGA3D_CLEAR_DEPTH;
    if (Flags & D3D10_DDI_CLEAR_STENCIL)
        svgaFlags |= SVGA3D_CLEAR_STENCIL;
    vgpu10ClearDepthStencilView(pDevice, svgaFlags, Stencil, pDepthStencilView->uDepthStencilViewId, Depth);
}


void vboxDXDestroyDepthStencilView(PVBOXDX_DEVICE pDevice, PVBOXDXDEPTHSTENCILVIEW pDepthStencilView)
{
    if (pDevice->pipeline.pDepthStencilView == pDepthStencilView)
    {
        DEBUG_BREAKPOINT_TEST();
    }

    RTListNodeRemove(&pDepthStencilView->nodeView);

    vgpu10DestroyDepthStencilView(pDevice, pDepthStencilView->uDepthStencilViewId);
    RTHandleTableFree(pDevice->hHTDepthStencilView, pDepthStencilView->uDepthStencilViewId);
}


void vboxDXSetRenderTargets(PVBOXDX_DEVICE pDevice, PVBOXDXDEPTHSTENCILVIEW pDepthStencilView,
                            uint32_t NumRTVs, UINT ClearSlots, PVBOXDXRENDERTARGETVIEW *papRenderTargetViews)
{
    /* Update the pipeline state. */
    for (unsigned i = 0; i < NumRTVs; ++i)
        pDevice->pipeline.apRenderTargetViews[i] = papRenderTargetViews[i];
    pDevice->pipeline.cRenderTargetViews = NumRTVs;

    for (unsigned i = 0; i < ClearSlots; ++i)
        pDevice->pipeline.apRenderTargetViews[NumRTVs + i] = NULL;

    pDevice->pipeline.pDepthStencilView = pDepthStencilView;

    /* Fetch view ids.*/
    uint32_t aRenderTargetViewIds[SVGA3D_MAX_SIMULTANEOUS_RENDER_TARGETS];
    for (unsigned i = 0; i < NumRTVs; ++i)
    {
        PVBOXDXRENDERTARGETVIEW pRenderTargetView = papRenderTargetViews[i];
        aRenderTargetViewIds[i] = pRenderTargetView ? pRenderTargetView->uRenderTargetViewId : SVGA3D_INVALID_ID;
    }

    uint32_t DepthStencilViewId = pDepthStencilView ? pDepthStencilView->uDepthStencilViewId : SVGA3D_INVALID_ID;

    vgpu10SetRenderTargets(pDevice, DepthStencilViewId, NumRTVs, ClearSlots, aRenderTargetViewIds);
}


void vboxDXSetShaderResourceViews(PVBOXDX_DEVICE pDevice, SVGA3dShaderType enmShaderType, uint32_t StartSlot,
                                  uint32_t NumViews, PVBOXDXSHADERRESOURCEVIEW const *papViews)
{
    Assert(NumViews <= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
    NumViews = RT_MIN(NumViews, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);

    /* Update the pipeline state. */
    PVBOXDXSRVSTATE pSRVS = &pDevice->pipeline.aSRVs[enmShaderType - SVGA3D_SHADERTYPE_MIN];

    for (unsigned i = 0; i < NumViews; ++i)
        pSRVS->apShaderResourceView[StartSlot + i] = papViews[i];

    uint32_t cSRV = RT_MAX(pSRVS->cShaderResourceView, StartSlot + NumViews);
    while (cSRV)
    {
        if (pSRVS->apShaderResourceView[cSRV - 1])
            break;
        --cSRV;
    }
    pSRVS->cShaderResourceView = cSRV;

    /* Fetch View ids. */
    uint32_t aViewIds[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    for (unsigned i = 0; i < NumViews; ++i)
    {
        VBOXDXSHADERRESOURCEVIEW *pView = papViews[i];
        aViewIds[i] = pView ? pView->uShaderResourceViewId : SVGA3D_INVALID_ID;
    }

    vgpu10SetShaderResources(pDevice, enmShaderType, StartSlot, NumViews, aViewIds);
}


void vboxDXSetConstantBuffers(PVBOXDX_DEVICE pDevice, SVGA3dShaderType enmShaderType, UINT StartSlot, UINT NumBuffers,
                              PVBOXDX_RESOURCE *papBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
    AssertReturnVoidStmt(  StartSlot < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT
                         && NumBuffers <= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT
                         && StartSlot + NumBuffers <= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
                         vboxDXDeviceSetError(pDevice, E_INVALIDARG));

    /* Remember which buffers must be set. The buffers will be actual set right before a draw call
     * because the host requires the updates of the buffers content to be done prior to setting the buffers.
     * SetSingleConstantBuffer command creates the actual buffer on the host using the current content,
     * so SetSingleConstantBuffer followed by Update will not update the buffer.
     */
    PVBOXDXCONSTANTBUFFERSSTATE pCBS = &pDevice->pipeline.aConstantBuffers[enmShaderType - SVGA3D_SHADERTYPE_MIN];

    for (unsigned i = 0; i < NumBuffers; ++i)
    {
        PVBOXDX_RESOURCE pResource = papBuffers[i];
        pCBS->apResource[StartSlot + i] = pResource;
        if (pResource)
        {
            uint32_t const cMaxConstants = pResource->AllocationDesc.cbAllocation / (4 * sizeof(UINT));
            uint32_t const FirstConstant = pFirstConstant ? pFirstConstant[i] : 0;
            uint32_t NumConstants = pNumConstants ? pNumConstants[i] : cMaxConstants;
            AssertReturnVoidStmt(FirstConstant < cMaxConstants,
                             pCBS->apResource[StartSlot + i] = NULL;
                             vboxDXDeviceSetError(pDevice, E_INVALIDARG));

            if (NumConstants > cMaxConstants - FirstConstant)
                NumConstants = cMaxConstants - FirstConstant;

            pCBS->aFirstConstant[StartSlot + i] = FirstConstant;
            pCBS->aNumConstants[StartSlot + i] = NumConstants;
        }
        else
        {
            pCBS->aFirstConstant[StartSlot + i] = 0;
            pCBS->aNumConstants[StartSlot + i] = 0;
        }
        LogFunc(("type %d, slot %d, first %d, num %d, cbAllocation %d",
                 enmShaderType, StartSlot + i, pCBS->aFirstConstant[StartSlot + i], pCBS->aNumConstants[StartSlot + i],
                 pResource ? pResource->AllocationDesc.cbAllocation : -1));
    }

    /* Join the current range and the new range. */
    if (pCBS->NumBuffers == 0)
    {
        pCBS->StartSlot = StartSlot;
        pCBS->NumBuffers = NumBuffers;
    }
    else
    {
        UINT FirstSlot = RT_MIN(StartSlot, pCBS->StartSlot);
        UINT EndSlot = RT_MAX(pCBS->StartSlot + pCBS->NumBuffers, StartSlot + NumBuffers);
        pCBS->StartSlot = FirstSlot;
        pCBS->NumBuffers = EndSlot - FirstSlot;
    }
}


void vboxDXResourceCopyRegion(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pDstResource, UINT DstSubresource,
                              UINT DstX, UINT DstY, UINT DstZ, PVBOXDX_RESOURCE pSrcResource, UINT SrcSubresource,
                              const D3D10_DDI_BOX *pSrcBox, UINT CopyFlags)
{
    RT_NOREF(CopyFlags);

    SVGA3dBox srcBox;
    if (pSrcBox)
    {
        srcBox.x = pSrcBox->left;
        srcBox.y = pSrcBox->top;
        srcBox.z = pSrcBox->front;
        srcBox.w = pSrcBox->right - pSrcBox->left;
        srcBox.h = pSrcBox->bottom - pSrcBox->top;
        srcBox.d = pSrcBox->back - pSrcBox->front;
    }
    else
    {
        vboxDXGetSubresourceBox(pSrcResource, SrcSubresource, &srcBox);
    }

    vgpu10ResourceCopyRegion(pDevice, vboxDXGetAllocation(pDstResource), DstSubresource,
                              DstX, DstY, DstZ, vboxDXGetAllocation(pSrcResource), SrcSubresource, srcBox);
}


void vboxDXResourceCopy(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pDstResource, PVBOXDX_RESOURCE pSrcResource)
{
    vgpu10ResourceCopy(pDevice, vboxDXGetAllocation(pDstResource), vboxDXGetAllocation(pSrcResource));
}


void vboxDXResourceResolveSubresource(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pDstResource, UINT DstSubresource,
                                      PVBOXDX_RESOURCE pSrcResource, UINT SrcSubresource, DXGI_FORMAT ResolveFormat)
{
    SVGA3dSurfaceFormat const copyFormat = vboxDXDxgiToSvgaFormat(ResolveFormat);
    vgpu10ResolveCopy(pDevice, vboxDXGetAllocation(pDstResource), DstSubresource,
                      vboxDXGetAllocation(pSrcResource), SrcSubresource, copyFormat);
}

static void vboxDXUndefineResourceViews(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource)
{
    VBOXDXSHADERRESOURCEVIEW *pShaderResourceView;
    RTListForEach(&pResource->listSRV, pShaderResourceView, VBOXDXSHADERRESOURCEVIEW, nodeView)
    {
        if (pShaderResourceView->fDefined)
        {
            vgpu10DestroyShaderResourceView(pDevice, pShaderResourceView->uShaderResourceViewId);
            pShaderResourceView->fDefined = false;
        }
    }

    VBOXDXRENDERTARGETVIEW *pRenderTargetView;
    RTListForEach(&pResource->listRTV, pRenderTargetView, VBOXDXRENDERTARGETVIEW, nodeView)
    {
        if (pRenderTargetView->fDefined)
        {
            vgpu10DestroyRenderTargetView(pDevice, pRenderTargetView->uRenderTargetViewId);
            pRenderTargetView->fDefined = false;
        }
    }

    VBOXDXDEPTHSTENCILVIEW *pDepthStencilView;
    RTListForEach(&pResource->listDSV, pDepthStencilView, VBOXDXDEPTHSTENCILVIEW, nodeView)
    {
        if (pDepthStencilView->fDefined)
        {
            vgpu10DestroyDepthStencilView(pDevice, pDepthStencilView->uDepthStencilViewId);
            pDepthStencilView->fDefined = false;
        }
    }

    VBOXDXUNORDEREDACCESSVIEW *pUnorderedAccessView;
    RTListForEach(&pResource->listUAV, pUnorderedAccessView, VBOXDXUNORDEREDACCESSVIEW, nodeView)
    {
        if (pUnorderedAccessView->fDefined)
        {
            vgpu10DestroyUAView(pDevice, pUnorderedAccessView->uUnorderedAccessViewId);
            pUnorderedAccessView->fDefined = false;
        }
    }

    VBOXDXVIDEODECODEROUTPUTVIEW *pVDOV;
    RTListForEach(&pResource->listVDOV, pVDOV, VBOXDXVIDEODECODEROUTPUTVIEW, nodeView)
    {
        if (pVDOV->fDefined)
        {
            vgpu10DestroyVideoDecoderOutputView(pDevice, pVDOV->uVideoDecoderOutputViewId);
            pVDOV->fDefined = false;
        }
    }

    VBOXDXVIDEOPROCESSORINPUTVIEW *pVPIV;
    RTListForEach(&pResource->listVPIV, pVPIV, VBOXDXVIDEOPROCESSORINPUTVIEW, nodeView)
    {
        if (pVPIV->fDefined)
        {
            vgpu10DestroyVideoProcessorInputView(pDevice, pVPIV->uVideoProcessorInputViewId);
            pVPIV->fDefined = false;
        }
    }


    VBOXDXVIDEOPROCESSOROUTPUTVIEW *pVPOV;
    RTListForEach(&pResource->listVPOV, pVPOV, VBOXDXVIDEOPROCESSOROUTPUTVIEW, nodeView)
    {
        if (pVPOV->fDefined)
        {
            vgpu10DestroyVideoProcessorOutputView(pDevice, pVPOV->uVideoProcessorOutputViewId);
            pVPOV->fDefined = false;
        }
    }
}


static void vboxDXRedefineResourceViews(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource)
{
    VBOXDXSHADERRESOURCEVIEW *pShaderResourceView;
    RTListForEach(&pResource->listSRV, pShaderResourceView, VBOXDXSHADERRESOURCEVIEW, nodeView)
    {
        if (!pShaderResourceView->fDefined)
        {
            vgpu10DefineShaderResourceView(pDevice, pShaderResourceView->uShaderResourceViewId, vboxDXGetAllocation(pShaderResourceView->pResource),
                                           pShaderResourceView->svga.format, pShaderResourceView->svga.resourceDimension,
                                           &pShaderResourceView->svga.desc);
            pShaderResourceView->fDefined = true;
        }
    }

    VBOXDXRENDERTARGETVIEW *pRenderTargetView;
    RTListForEach(&pResource->listRTV, pRenderTargetView, VBOXDXRENDERTARGETVIEW, nodeView)
    {
        if (!pRenderTargetView->fDefined)
        {
            vgpu10DefineRenderTargetView(pDevice, pRenderTargetView->uRenderTargetViewId, vboxDXGetAllocation(pRenderTargetView->pResource),
                                         pRenderTargetView->svga.format, pRenderTargetView->svga.resourceDimension,
                                         &pRenderTargetView->svga.desc);
            pRenderTargetView->fDefined = true;
        }
    }

    VBOXDXDEPTHSTENCILVIEW *pDepthStencilView;
    RTListForEach(&pResource->listDSV, pDepthStencilView, VBOXDXDEPTHSTENCILVIEW, nodeView)
    {
        if (!pDepthStencilView->fDefined)
        {
            vgpu10DefineDepthStencilView(pDevice, pDepthStencilView->uDepthStencilViewId, vboxDXGetAllocation(pDepthStencilView->pResource),
                                         pDepthStencilView->svga.format, pDepthStencilView->svga.resourceDimension,
                                         pDepthStencilView->svga.mipSlice, pDepthStencilView->svga.firstArraySlice,
                                         pDepthStencilView->svga.arraySize, pDepthStencilView->svga.flags);
            pDepthStencilView->fDefined = true;
        }
    }

    VBOXDXUNORDEREDACCESSVIEW *pUnorderedAccessView;
    RTListForEach(&pResource->listUAV, pUnorderedAccessView, VBOXDXUNORDEREDACCESSVIEW, nodeView)
    {
        if (!pUnorderedAccessView->fDefined)
        {
            vgpu10DefineUAView(pDevice, pUnorderedAccessView->uUnorderedAccessViewId, vboxDXGetAllocation(pUnorderedAccessView->pResource),
                               pUnorderedAccessView->svga.format, pUnorderedAccessView->svga.resourceDimension,
                               pUnorderedAccessView->svga.desc);
            pUnorderedAccessView->fDefined = true;
        }
    }

    VBOXDXVIDEODECODEROUTPUTVIEW *pVDOV;
    RTListForEach(&pResource->listVDOV, pVDOV, VBOXDXVIDEODECODEROUTPUTVIEW, nodeView)
    {
        if (!pVDOV->fDefined)
        {
            vgpu10DefineVideoDecoderOutputView(pDevice, pVDOV->uVideoDecoderOutputViewId,
                                               vboxDXGetAllocation(pVDOV->pResource),
                                               pVDOV->svga.desc);
            pVDOV->fDefined = true;
        }
    }

    VBOXDXVIDEOPROCESSORINPUTVIEW *pVPIV;
    RTListForEach(&pResource->listVPIV, pVPIV, VBOXDXVIDEOPROCESSORINPUTVIEW, nodeView)
    {
        if (!pVPIV->fDefined)
        {
            vgpu10DefineVideoProcessorInputView(pDevice, pVPIV->uVideoProcessorInputViewId,
                                                vboxDXGetAllocation(pVPIV->pResource),
                                                pVPIV->svga.ContentDesc, pVPIV->svga.VPIVDesc);
            pVPIV->fDefined = true;
        }
    }

    VBOXDXVIDEOPROCESSOROUTPUTVIEW *pVPOV;
    RTListForEach(&pResource->listVPOV, pVPOV, VBOXDXVIDEOPROCESSOROUTPUTVIEW, nodeView)
    {
        if (!pVPOV->fDefined)
        {
            vgpu10DefineVideoProcessorOutputView(pDevice, pVPOV->uVideoProcessorOutputViewId,
                                                 vboxDXGetAllocation(pVPOV->pResource),
                                                 pVPOV->svga.ContentDesc, pVPOV->svga.VPOVDesc);
            pVPOV->fDefined = true;
        }
    }
}


static void vboxdxUnbindResourceViews(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource)
{
    VBOXDXSHADERRESOURCEVIEW *pShaderResourceView;
    RTListForEach(&pResource->listSRV, pShaderResourceView, VBOXDXSHADERRESOURCEVIEW, nodeView)
    {
        /* Search this view in the pipeline state. */
        for (unsigned idxShaderType = 0; idxShaderType < RT_ELEMENTS(pDevice->pipeline.aSRVs); ++idxShaderType)
        {
            SVGA3dShaderType const enmShaderType = (SVGA3dShaderType)(idxShaderType + SVGA3D_SHADERTYPE_MIN);

            PVBOXDXSRVSTATE pSRVS = &pDevice->pipeline.aSRVs[idxShaderType];
            for (unsigned i = 0; i < pSRVS->cShaderResourceView; ++i)
            {
                if (pSRVS->apShaderResourceView[i] == pShaderResourceView)
                {
                    uint32_t id = SVGA3D_INVALID_ID;
                    vgpu10SetShaderResources(pDevice, enmShaderType, i, 1, &id);
                }
            }
        }
    }
}


HRESULT vboxDXRotateResourceIdentities(PVBOXDX_DEVICE pDevice, UINT cResources, PVBOXDX_RESOURCE *papResources)
{
#ifdef LOG_ENABLED
    for (unsigned i = 0; i < cResources; ++i)
    {
        PVBOXDX_RESOURCE pResource = papResources[i];
        LogFlowFunc(("Resources[%d]: pResource %p, hAllocation 0x%08x\n", i, pResource, vboxDXGetAllocation(pResource)));

        unsigned iV = 0;
        VBOXDXRENDERTARGETVIEW *pRTV;
        RTListForEach(&pResource->listRTV, pRTV, VBOXDXRENDERTARGETVIEW, nodeView)
        {
            LogFlowFunc(("  RTV[%d]: %p\n", iV, pRTV));
            ++iV;
        }

        iV = 0;
        VBOXDXSHADERRESOURCEVIEW *pSRV;
        RTListForEach(&pResource->listSRV, pSRV, VBOXDXSHADERRESOURCEVIEW, nodeView)
        {
            LogFlowFunc(("  SRV[%d]: %p\n", iV, pSRV));
            ++iV;
        }
    }

    LogFlowFunc(("Pipeline: cRTV %u, cSRV VS %u, PS %u, GS %u, HS %u, DS %u, CS %u\n",
                 pDevice->pipeline.cRenderTargetViews,
                 pDevice->pipeline.aSRVs[0].cShaderResourceView,
                 pDevice->pipeline.aSRVs[1].cShaderResourceView,
                 pDevice->pipeline.aSRVs[2].cShaderResourceView,
                 pDevice->pipeline.aSRVs[3].cShaderResourceView,
                 pDevice->pipeline.aSRVs[4].cShaderResourceView,
                 pDevice->pipeline.aSRVs[5].cShaderResourceView));
    for (unsigned i = 0; i < pDevice->pipeline.cRenderTargetViews; ++i)
        LogFlowFunc(("  RTV[%d]: %p\n", i, pDevice->pipeline.apRenderTargetViews[i]));
    for (unsigned i = 0; i < pDevice->pipeline.aSRVs[0].cShaderResourceView; ++i)
        LogFlowFunc(("  SRV VS[%d]: %p\n", i, pDevice->pipeline.aSRVs[0].apShaderResourceView[i]));
    for (unsigned i = 0; i < pDevice->pipeline.aSRVs[1].cShaderResourceView; ++i)
        LogFlowFunc(("  SRV PS[%d]: %p\n", i, pDevice->pipeline.aSRVs[1].apShaderResourceView[i]));
    for (unsigned i = 0; i < pDevice->pipeline.aSRVs[2].cShaderResourceView; ++i)
        LogFlowFunc(("  SRV GS[%d]: %p\n", i, pDevice->pipeline.aSRVs[2].apShaderResourceView[i]));
    for (unsigned i = 0; i < pDevice->pipeline.aSRVs[3].cShaderResourceView; ++i)
        LogFlowFunc(("  SRV HS[%d]: %p\n", i, pDevice->pipeline.aSRVs[3].apShaderResourceView[i]));
    for (unsigned i = 0; i < pDevice->pipeline.aSRVs[4].cShaderResourceView; ++i)
        LogFlowFunc(("  SRV DS[%d]: %p\n", i, pDevice->pipeline.aSRVs[4].apShaderResourceView[i]));
    for (unsigned i = 0; i < pDevice->pipeline.aSRVs[5].cShaderResourceView; ++i)
        LogFlowFunc(("  SRV CS[%d]: %p\n", i, pDevice->pipeline.aSRVs[5].apShaderResourceView[i]));
#endif

    /** @todo Rebind UAVs which are currently bound to pipeline. */

    /* Unbind current render targets, if a resource is bound as a render target. */
    for (unsigned i = 0; i < cResources; ++i)
    {
        PVBOXDX_RESOURCE pResource = papResources[i];

        bool fBound = false;
        VBOXDXRENDERTARGETVIEW *pRenderTargetView;
        RTListForEach(&pResource->listRTV, pRenderTargetView, VBOXDXRENDERTARGETVIEW, nodeView)
        {
            for (unsigned iRTV = 0; iRTV < pDevice->pipeline.cRenderTargetViews; ++iRTV)
            {
                if (pDevice->pipeline.apRenderTargetViews[iRTV] == pRenderTargetView)
                {
                    fBound = true;
                    break;
               }
            }
            if (fBound)
                break;
        }

        if (!fBound)
        {
            VBOXDXDEPTHSTENCILVIEW *pDepthStencilView;
            RTListForEach(&pResource->listDSV, pDepthStencilView, VBOXDXDEPTHSTENCILVIEW, nodeView)
            {
                if (pDevice->pipeline.pDepthStencilView == pDepthStencilView)
                {
                    fBound = true;
                    break;
                }
            }
        }

        if (fBound)
        {
            vgpu10SetRenderTargets(pDevice, SVGA3D_INVALID_ID, 0, SVGA3D_MAX_SIMULTANEOUS_RENDER_TARGETS, NULL);
            break;
        }
    }

    /* Inform the host that views of these resources are not valid anymore. */
    for (unsigned i = 0; i < cResources; ++i)
    {
        PVBOXDX_RESOURCE pResource = papResources[i];
        vboxdxUnbindResourceViews(pDevice, pResource);
        vboxDXUndefineResourceViews(pDevice, pResource);
    }

    /* Rotate allocation handles. The function would be that simple if resources would not have views. */
    D3DKMT_HANDLE const hAllocation = papResources[0]->pKMResource->hAllocation;
    for (unsigned i = 0; i < cResources - 1; ++i)
        papResources[i]->pKMResource->hAllocation = papResources[i + 1]->pKMResource->hAllocation;
    papResources[cResources - 1]->pKMResource->hAllocation = hAllocation;

    /* Recreate views for the new hAllocations. */
    for (unsigned i = 0; i < cResources; ++i)
    {
        PVBOXDX_RESOURCE pResource = papResources[i];
        vboxDXRedefineResourceViews(pDevice, pResource);
    }

    /* Reapply pipeline state. "Also, the driver might be required to reapply currently bound views." */
    pDevice->pUMCallbacks->pfnStateVsSrvCb(pDevice->hRTCoreLayer, /* Base */ 0, /* Count */ SVGA3D_DX_MAX_SRVIEWS);
    pDevice->pUMCallbacks->pfnStateGsSrvCb(pDevice->hRTCoreLayer, /* Base */ 0, /* Count */ SVGA3D_DX_MAX_SRVIEWS);
    pDevice->pUMCallbacks->pfnStatePsSrvCb(pDevice->hRTCoreLayer, /* Base */ 0, /* Count */ SVGA3D_DX_MAX_SRVIEWS);
    if (pDevice->uDDIVersion >= D3D11_0_DDI_INTERFACE_VERSION)
    {
        pDevice->pUMCallbacks->pfnStateHsSrvCb(pDevice->hRTCoreLayer, /* Base */ 0, /* Count */ SVGA3D_DX_MAX_SRVIEWS);
        pDevice->pUMCallbacks->pfnStateDsSrvCb(pDevice->hRTCoreLayer, /* Base */ 0, /* Count */ SVGA3D_DX_MAX_SRVIEWS);
        pDevice->pUMCallbacks->pfnStateCsSrvCb(pDevice->hRTCoreLayer, /* Base */ 0, /* Count */ SVGA3D_DX_MAX_SRVIEWS);
        UINT cUAV = pDevice->uDDIVersion >= D3D11_1_DDI_INTERFACE_VERSION
                  ? SVGA3D_DX11_1_MAX_UAVIEWS
                  : SVGA3D_MAX_UAVIEWS;
        pDevice->pUMCallbacks->pfnStateCsUavCb(pDevice->hRTCoreLayer, /* Base */ 0, cUAV);
    }

    pDevice->pUMCallbacks->pfnStateOmRenderTargetsCb(pDevice->hRTCoreLayer);

    return S_OK;
}


HRESULT vboxDXOfferResources(PVBOXDX_DEVICE pDevice, UINT cResources, PVBOXDX_RESOURCE *papResources, D3DDDI_OFFER_PRIORITY Priority)
{
#if 1
    /** @todo Later. */
    RT_NOREF(pDevice, cResources, papResources, Priority);
#else
    uint32_t const cbAlloc = cResources * (sizeof(HANDLE) + sizeof(D3DKMT_HANDLE));
    uint8_t *pu8 = (uint8_t *)RTMemAlloc(cbAlloc);
    AssertReturn(pu8, E_OUTOFMEMORY);

    HANDLE *pahResources = (HANDLE *)pu8;
    D3DKMT_HANDLE *pahAllocations = (D3DKMT_HANDLE *)(pu8 + cResources * sizeof(HANDLE));
    for (unsigned i = 0; i < cResources; ++i)
    {
        PVBOXDX_RESOURCE pResource = papResources[i];
        pahResources[i] = pResource->hRTResource.handle;
        pahAllocations[i] = vboxDXGetAllocation(pResource);
    }

    D3DDDICB_OFFERALLOCATIONS ddiOfferAllocations;
    RT_ZERO(ddiOfferAllocations);
    ddiOfferAllocations.pResources = pahResources;
    ddiOfferAllocations.HandleList = pahAllocations;
    ddiOfferAllocations.NumAllocations = cResources;
    ddiOfferAllocations.Priority = Priority;

    HRESULT hr = pDevice->pRTCallbacks->pfnOfferAllocationsCb(pDevice->hRTDevice.handle, &ddiOfferAllocations);
    LogFlowFunc(("pfnOfferAllocationsCb returned %d", hr));

    RTMemFree(pu8);

    AssertReturnStmt(SUCCEEDED(hr), vboxDXDeviceSetError(pDevice, hr), hr);
#endif
    return S_OK;
}


HRESULT vboxDXReclaimResources(PVBOXDX_DEVICE pDevice, UINT cResources, PVBOXDX_RESOURCE *papResources, BOOL *paDiscarded)
{
#if 1
    /** @todo Later. */
    RT_NOREF(pDevice, cResources, papResources, paDiscarded);
#else
    uint32_t const cbAlloc = cResources * (sizeof(HANDLE) + sizeof(D3DKMT_HANDLE));
    uint8_t *pu8 = (uint8_t *)RTMemAlloc(cbAlloc);
    AssertReturn(pu8, E_OUTOFMEMORY);

    HANDLE *pahResources = (HANDLE *)pu8;
    D3DKMT_HANDLE *pahAllocations = (D3DKMT_HANDLE *)(pu8 + cResources * sizeof(HANDLE));
    for (unsigned i = 0; i < cResources; ++i)
    {
        PVBOXDX_RESOURCE pResource = papResources[i];
        pahResources[i] = pResource->hRTResource.handle;
        pahAllocations[i] = vboxDXGetAllocation(pResource);
    }

    D3DDDICB_RECLAIMALLOCATIONS ddiReclaimAllocations;
    RT_ZERO(ddiReclaimAllocations);
    ddiReclaimAllocations.pResources = pahResources;
    ddiReclaimAllocations.HandleList = pahAllocations;
    ddiReclaimAllocations.pDiscarded = paDiscarded;
    ddiReclaimAllocations.NumAllocations = cResources;

    HRESULT hr = pDevice->pRTCallbacks->pfnReclaimAllocationsCb(pDevice->hRTDevice.handle, &ddiReclaimAllocations);
    LogFlowFunc(("pfnReclaimAllocationsCb returned %d", hr));

    RTMemFree(pu8);

    AssertReturnStmt(SUCCEEDED(hr), vboxDXDeviceSetError(pDevice, hr), hr);
#endif
    return S_OK;
}


void vboxDXCreateUnorderedAccessView(PVBOXDX_DEVICE pDevice, PVBOXDXUNORDEREDACCESSVIEW pUnorderedAccessView)
{
    int rc = RTHandleTableAlloc(pDevice->hHTUnorderedAccessView, pUnorderedAccessView, &pUnorderedAccessView->uUnorderedAccessViewId);
    AssertRCReturnVoidStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

    pUnorderedAccessView->svga.format            = vboxDXDxgiToSvgaFormat(pUnorderedAccessView->Format);
    pUnorderedAccessView->svga.resourceDimension = d3dToSvgaResourceDimension(pUnorderedAccessView->ResourceDimension);
    SVGA3dUAViewDesc *pDesc                      = &pUnorderedAccessView->svga.desc;
    RT_ZERO(*pDesc);
    switch (pUnorderedAccessView->ResourceDimension)
    {
        case D3D10DDIRESOURCE_BUFFER:
            pDesc->buffer.firstElement   = pUnorderedAccessView->DimensionDesc.Buffer.FirstElement;
            pDesc->buffer.numElements    = pUnorderedAccessView->DimensionDesc.Buffer.NumElements;
            pDesc->buffer.flags          = pUnorderedAccessView->DimensionDesc.Buffer.Flags;
            break;
        case D3D10DDIRESOURCE_TEXTURE1D:
            pDesc->tex.mipSlice          = pUnorderedAccessView->DimensionDesc.Tex1D.MipSlice;
            pDesc->tex.firstArraySlice   = pUnorderedAccessView->DimensionDesc.Tex1D.FirstArraySlice;
            pDesc->tex.arraySize         = pUnorderedAccessView->DimensionDesc.Tex1D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURE2D:
            pDesc->tex.mipSlice          = pUnorderedAccessView->DimensionDesc.Tex2D.MipSlice;
            pDesc->tex.firstArraySlice   = pUnorderedAccessView->DimensionDesc.Tex2D.FirstArraySlice;
            pDesc->tex.arraySize         = pUnorderedAccessView->DimensionDesc.Tex2D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURE3D:
            pDesc->tex3D.mipSlice        = pUnorderedAccessView->DimensionDesc.Tex3D.MipSlice;
            pDesc->tex3D.firstW          = pUnorderedAccessView->DimensionDesc.Tex3D.FirstW;
            pDesc->tex3D.wSize           = pUnorderedAccessView->DimensionDesc.Tex3D.WSize;
            break;
        default:
            RTHandleTableFree(pDevice->hHTUnorderedAccessView, pUnorderedAccessView->uUnorderedAccessViewId);
            vboxDXDeviceSetError(pDevice, E_INVALIDARG);
            return;
    }

    vgpu10DefineUAView(pDevice, pUnorderedAccessView->uUnorderedAccessViewId, vboxDXGetAllocation(pUnorderedAccessView->pResource),
                       pUnorderedAccessView->svga.format, pUnorderedAccessView->svga.resourceDimension,
                       pUnorderedAccessView->svga.desc);

    pUnorderedAccessView->fDefined = true;
    RTListAppend(&pUnorderedAccessView->pResource->listUAV, &pUnorderedAccessView->nodeView);
}


void vboxDXDestroyUnorderedAccessView(PVBOXDX_DEVICE pDevice, PVBOXDXUNORDEREDACCESSVIEW pUnorderedAccessView)
{
    RTListNodeRemove(&pUnorderedAccessView->nodeView);

    vgpu10DestroyUAView(pDevice, pUnorderedAccessView->uUnorderedAccessViewId);
    RTHandleTableFree(pDevice->hHTUnorderedAccessView, pUnorderedAccessView->uUnorderedAccessViewId);
}


void vboxDXClearUnorderedAccessViewUint(PVBOXDX_DEVICE pDevice, PVBOXDXUNORDEREDACCESSVIEW pUnorderedAccessView, const UINT Values[4])
{
    vgpu10ClearUAViewUint(pDevice, pUnorderedAccessView->uUnorderedAccessViewId, Values);
}


void vboxDXClearUnorderedAccessViewFloat(PVBOXDX_DEVICE pDevice, PVBOXDXUNORDEREDACCESSVIEW pUnorderedAccessView, const FLOAT Values[4])
{
    vgpu10ClearUAViewFloat(pDevice, pUnorderedAccessView->uUnorderedAccessViewId, Values);
}


void vboxDXCsSetUnorderedAccessViews(PVBOXDX_DEVICE pDevice, UINT StartSlot, UINT NumViews, const uint32_t *paViewIds, const UINT* pUAVInitialCounts)
{
    for (unsigned i = 0; i < NumViews; ++i)
    {
        if (paViewIds[i] != SVGA3D_INVALID_ID)
            vgpu10SetStructureCount(pDevice, paViewIds[i], pUAVInitialCounts[i]);
    }

    vgpu10SetCSUAViews(pDevice, StartSlot, NumViews, paViewIds);
}


void vboxDXSetUnorderedAccessViews(PVBOXDX_DEVICE pDevice, UINT StartSlot, UINT NumViews, const PVBOXDXUNORDEREDACCESSVIEW *papViews,
                                   const UINT *pUAVInitialCounts)
{
    /* Fetch view ids.*/
    uint32_t aViewIds[D3D11_1_UAV_SLOT_COUNT];
    for (unsigned i = 0; i < NumViews; ++i)
    {
        PVBOXDXUNORDEREDACCESSVIEW pUnorderedAccessView = papViews[i];
        aViewIds[i] = pUnorderedAccessView ? pUnorderedAccessView->uUnorderedAccessViewId : SVGA3D_INVALID_ID;
    }

    UINT NumViewsToSet;
    if (pDevice->pipeline.cUnorderedAccessViews > NumViews)
    {
        /* Clear previously set views, which are not used anymore. */
        for (unsigned i = NumViews; i < pDevice->pipeline.cUnorderedAccessViews; ++i)
            aViewIds[i] = SVGA3D_INVALID_ID;

        NumViewsToSet = pDevice->pipeline.cUnorderedAccessViews;
    }
    else
        NumViewsToSet = NumViews;

    pDevice->pipeline.cUnorderedAccessViews = NumViews;

    for (unsigned i = 0; i < NumViews; ++i)
    {
        if (aViewIds[i] != SVGA3D_INVALID_ID)
            vgpu10SetStructureCount(pDevice, aViewIds[i], pUAVInitialCounts[i]);
    }

    vgpu10SetUAViews(pDevice, StartSlot, NumViewsToSet, aViewIds);
}


void vboxDXDispatch(PVBOXDX_DEVICE pDevice, UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ)
{
    vboxDXSetupPipeline(pDevice);
    vgpu10Dispatch(pDevice, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
}


void vboxDXDispatchIndirect(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource, UINT AlignedByteOffsetForArgs)
{
    vboxDXSetupPipeline(pDevice);
    vgpu10DispatchIndirect(pDevice, vboxDXGetAllocation(pResource), AlignedByteOffsetForArgs);
}


void vboxDXCopyStructureCount(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pDstBuffer, UINT DstAlignedByteOffset, PVBOXDXUNORDEREDACCESSVIEW pSrcView)
{
    vgpu10CopyStructureCount(pDevice, pSrcView->uUnorderedAccessViewId, vboxDXGetAllocation(pDstBuffer), DstAlignedByteOffset);
}


HRESULT vboxDXBlt(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pDstResource, UINT DstSubresource,
                  PVBOXDX_RESOURCE pSrcResource, UINT SrcSubresource,
                  UINT DstLeft, UINT DstTop, UINT DstRight, UINT DstBottom,
                  DXGI_DDI_ARG_BLT_FLAGS Flags, DXGI_DDI_MODE_ROTATION Rotate)
{
    AssertReturn(Rotate == DXGI_DDI_MODE_ROTATION_IDENTITY, DXGI_ERROR_INVALID_CALL);
    AssertReturn(Flags.Resolve == 0, DXGI_ERROR_INVALID_CALL); /** @todo Multisampled resources. */

    SVGA3dBox boxSrc;
    vboxDXGetSubresourceBox(pSrcResource, SrcSubresource, &boxSrc); /* Entire subresource. */

    SVGA3dBox boxDest;
    boxDest.x = DstLeft;
    boxDest.y = DstTop;
    boxDest.z = 0;
    boxDest.w = DstRight - DstLeft;
    boxDest.h = DstBottom - DstTop;
    boxDest.d = 1;

    SVGA3dDXPresentBltMode mode = 0;

    vgpu10PresentBlt(pDevice, vboxDXGetAllocation(pSrcResource), SrcSubresource, vboxDXGetAllocation(pDstResource), DstSubresource,
                     boxSrc, boxDest, mode);
    return S_OK;
}


void vboxDXClearView(PVBOXDX_DEVICE pDevice, D3D11DDI_HANDLETYPE ViewType, uint32_t ViewId, FLOAT const Color[4], D3D10_DDI_RECT const *pRect, UINT NumRects)
{
    SVGAFifo3dCmdId enmCmdId = VBSVGA_3D_CMD_DX_CLEAR_RTV;
    switch (ViewType)
    {
        case D3D10DDI_HT_RENDERTARGETVIEW:
            break;
        case D3D11DDI_HT_UNORDEREDACCESSVIEW:
            enmCmdId = VBSVGA_3D_CMD_DX_CLEAR_UAV;
            break;
        case D3D11_1DDI_HT_VIDEODECODEROUTPUTVIEW:
            enmCmdId = VBSVGA_3D_CMD_DX_CLEAR_VDOV;
            break;
        case D3D11_1DDI_HT_VIDEOPROCESSORINPUTVIEW:
            enmCmdId = VBSVGA_3D_CMD_DX_CLEAR_VPIV;
            break;
        case D3D11_1DDI_HT_VIDEOPROCESSOROUTPUTVIEW:
            enmCmdId = VBSVGA_3D_CMD_DX_CLEAR_VPOV;
            break;
        default:
            AssertFailedReturnVoid();
    }

    vgpu10ClearView(pDevice, enmCmdId, ViewId, Color, pRect, NumRects);
}


static void dxDeallocateStagingResources(PVBOXDX_DEVICE pDevice)
{
    /* Move staging resources to the deferred destruction queue. */
    PVBOXDXKMRESOURCE pKMResource, pNextKMResource;
    RTListForEachSafe(&pDevice->listStagingResources, pKMResource, pNextKMResource, VBOXDXKMRESOURCE, nodeStaging)
    {
        RTListNodeRemove(&pKMResource->nodeStaging);

        PVBOXDX_RESOURCE pStagingResource = pKMResource->pResource;
        pKMResource->pResource = NULL;

        Assert(pStagingResource->pKMResource == pKMResource);

        /* Remove from the list of active resources. */
        RTListNodeRemove(&pKMResource->nodeResource);
        RTListAppend(&pDevice->listDestroyedResources, &pKMResource->nodeResource);

        /* Staging resources are allocated by the driver. */
        RTMemFree(pStagingResource);
    }
}


static void dxDestroyDeferredResources(PVBOXDX_DEVICE pDevice)
{
    PVBOXDXKMRESOURCE pKMResource, pNext;
    RTListForEachSafe(&pDevice->listDestroyedResources, pKMResource, pNext, VBOXDXKMRESOURCE, nodeResource)
    {
        RTListNodeRemove(&pKMResource->nodeResource);

        D3DDDICB_DEALLOCATE ddiDeallocate;
        RT_ZERO(ddiDeallocate);
        //ddiDeallocate.hResource      = NULL;
        ddiDeallocate.NumAllocations = 1;
        ddiDeallocate.HandleList     = &pKMResource->hAllocation;

        HRESULT hr = pDevice->pRTCallbacks->pfnDeallocateCb(pDevice->hRTDevice.handle, &ddiDeallocate);
        LogFlowFunc(("pfnDeallocateCb returned %d", hr));
        AssertStmt(SUCCEEDED(hr), vboxDXDeviceSetError(pDevice, hr));

        RTMemFree(pKMResource);
    }
}


HRESULT vboxDXFlush(PVBOXDX_DEVICE pDevice, bool fForce)
{
    if (   pDevice->cbCommandBuffer != 0
        || fForce)
    {
        HRESULT hr = vboxDXDeviceFlushCommands(pDevice);
        AssertReturnStmt(SUCCEEDED(hr), vboxDXDeviceSetError(pDevice, hr), hr);
    }

    /* Free the staging resources which used for uploads in this command buffer.
     * They are moved to the deferred destruction queue.
     */
    dxDeallocateStagingResources(pDevice);

    /* Process deferred-destruction queue. */
    dxDestroyDeferredResources(pDevice);

    return S_OK;
}


/*
 *
 * D3D device initialization/termination.
 *
 */

static HRESULT vboxDXCreateKernelContextForDevice(PVBOXDX_DEVICE pDevice)
{
    HRESULT hr;

    VBOXWDDM_CREATECONTEXT_INFO privateData;
    RT_ZERO(privateData);
    privateData.enmType = VBOXWDDM_CONTEXT_TYPE_VMSVGA_D3D;
    privateData.u32IfVersion = 11; /** @todo This is not really used by miniport. */
    privateData.u.vmsvga.u32Flags = VBOXWDDM_F_GA_CONTEXT_VGPU10;

    D3DDDICB_CREATECONTEXT ddiCreateContext;
    RT_ZERO(ddiCreateContext);
    ddiCreateContext.pPrivateDriverData = &privateData;
    ddiCreateContext.PrivateDriverDataSize = sizeof(privateData);

    hr = pDevice->pRTCallbacks->pfnCreateContextCb(pDevice->hRTDevice.handle, &ddiCreateContext);
    LogFlowFunc(("hr %d, hContext 0x%p, CommandBufferSize 0x%x, AllocationListSize 0x%x, PatchLocationListSize 0x%x",
        hr, ddiCreateContext.hContext, ddiCreateContext.CommandBufferSize,
        ddiCreateContext.AllocationListSize, ddiCreateContext.PatchLocationListSize));
    if (SUCCEEDED(hr))
    {
        pDevice->hContext              = ddiCreateContext.hContext;
        pDevice->pCommandBuffer        = ddiCreateContext.pCommandBuffer;
        pDevice->CommandBufferSize     = ddiCreateContext.CommandBufferSize;
        pDevice->pAllocationList       = ddiCreateContext.pAllocationList;
        pDevice->AllocationListSize    = ddiCreateContext.AllocationListSize;
        pDevice->pPatchLocationList    = ddiCreateContext.pPatchLocationList;
        pDevice->PatchLocationListSize = ddiCreateContext.PatchLocationListSize;

        pDevice->cbCommandBuffer   = 0;
        pDevice->cbCommandReserved = 0;
    }
    return hr;
}


static int vboxDXDeviceCreateObjects(PVBOXDX_DEVICE pDevice)
{
    int rc;

    rc = RTHandleTableCreateEx(&pDevice->hHTBlendState, /* fFlags */ 0, /* uBase */ 0,
                               D3D10_REQ_BLEND_OBJECT_COUNT_PER_CONTEXT, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTDepthStencilState, /* fFlags */ 0, /* uBase */ 0,
                               D3D10_REQ_DEPTH_STENCIL_OBJECT_COUNT_PER_CONTEXT, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTRasterizerState, /* fFlags */ 0, /* uBase */ 0,
                               D3D10_REQ_RASTERIZER_OBJECT_COUNT_PER_CONTEXT, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTSamplerState, /* fFlags */ 0, /* uBase */ 0,
                               D3D10_REQ_SAMPLER_OBJECT_COUNT_PER_CONTEXT, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTElementLayout, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTShader, /* fFlags */ 0, /* uBase */ 0,
                               SVGA3D_MAX_SHADERIDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTShaderResourceView, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTRenderTargetView, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTDepthStencilView, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTQuery, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTUnorderedAccessView, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTStreamOutput, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTVideoProcessor, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTVideoDecoderOutputView, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTVideoDecoder, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTVideoProcessorInputView, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    rc = RTHandleTableCreateEx(&pDevice->hHTVideoProcessorOutputView, /* fFlags */ 0, /* uBase */ 0,
                               SVGA_COTABLE_MAX_IDS, NULL, NULL);
    AssertRCReturn(rc, rc);

    RTListInit(&pDevice->listResources);
    RTListInit(&pDevice->listDestroyedResources);
    RTListInit(&pDevice->listStagingResources);
    RTListInit(&pDevice->listShaders);
    RTListInit(&pDevice->listQueries);
    RTListInit(&pDevice->listCOAQuery);
    RTListInit(&pDevice->listCOAStreamOutput);

    pDevice->u64MobFenceValue = 0;

    return rc;
}


static void vboxDXDeviceDeleteObjects(PVBOXDX_DEVICE pDevice)
{
    if (pDevice->hHTBlendState)
    {
        RTHandleTableDestroy(pDevice->hHTBlendState, NULL, NULL);
        pDevice->hHTBlendState = 0;
    }

    if (pDevice->hHTDepthStencilState)
    {
        RTHandleTableDestroy(pDevice->hHTDepthStencilState, NULL, NULL);
        pDevice->hHTDepthStencilState = 0;
    }

    if (pDevice->hHTRasterizerState)
    {
        RTHandleTableDestroy(pDevice->hHTRasterizerState, NULL, NULL);
        pDevice->hHTRasterizerState = 0;
    }

    if (pDevice->hHTSamplerState)
    {
        RTHandleTableDestroy(pDevice->hHTSamplerState, NULL, NULL);
        pDevice->hHTSamplerState = 0;
    }

    if (pDevice->hHTElementLayout)
    {
        RTHandleTableDestroy(pDevice->hHTElementLayout, NULL, NULL);
        pDevice->hHTElementLayout = 0;
    }

    if (pDevice->hHTShader)
    {
        RTHandleTableDestroy(pDevice->hHTShader, NULL, NULL);
        pDevice->hHTShader = 0;
    }

    if (pDevice->hHTShaderResourceView)
    {
        RTHandleTableDestroy(pDevice->hHTShaderResourceView, NULL, NULL);
        pDevice->hHTShaderResourceView = 0;
    }

    if (pDevice->hHTRenderTargetView)
    {
        RTHandleTableDestroy(pDevice->hHTRenderTargetView, NULL, NULL);
        pDevice->hHTRenderTargetView = 0;
    }

    if (pDevice->hHTDepthStencilView)
    {
        RTHandleTableDestroy(pDevice->hHTDepthStencilView, NULL, NULL);
        pDevice->hHTDepthStencilView = 0;
    }

    if (pDevice->hHTQuery)
    {
        RTHandleTableDestroy(pDevice->hHTQuery, NULL, NULL);
        pDevice->hHTQuery = 0;
    }

    if (pDevice->hHTUnorderedAccessView)
    {
        RTHandleTableDestroy(pDevice->hHTUnorderedAccessView, NULL, NULL);
        pDevice->hHTUnorderedAccessView = 0;
    }

    if (pDevice->hHTStreamOutput)
    {
        RTHandleTableDestroy(pDevice->hHTStreamOutput, NULL, NULL);
        pDevice->hHTStreamOutput = 0;
    }

    if (pDevice->hHTVideoProcessor)
    {
        RTHandleTableDestroy(pDevice->hHTVideoProcessor, NULL, NULL);
        pDevice->hHTVideoProcessor = 0;
    }

    if (pDevice->hHTVideoDecoderOutputView)
    {
        RTHandleTableDestroy(pDevice->hHTVideoDecoderOutputView, NULL, NULL);
        pDevice->hHTVideoDecoderOutputView = 0;
    }

    if (pDevice->hHTVideoDecoder)
    {
        RTHandleTableDestroy(pDevice->hHTVideoDecoder, NULL, NULL);
        pDevice->hHTVideoDecoder = 0;
    }

    if (pDevice->hHTVideoProcessorInputView)
    {
        RTHandleTableDestroy(pDevice->hHTVideoProcessorInputView, NULL, NULL);
        pDevice->hHTVideoProcessorInputView = 0;
    }

    if (pDevice->hHTVideoProcessorOutputView)
    {
        RTHandleTableDestroy(pDevice->hHTVideoProcessorOutputView, NULL, NULL);
        pDevice->hHTVideoProcessorOutputView = 0;
    }
}


HRESULT vboxDXDeviceInit(PVBOXDX_DEVICE pDevice)
{
    HRESULT hr = vboxDXCreateKernelContextForDevice(pDevice);
    AssertReturn(SUCCEEDED(hr), hr);

    int rc = vboxDXDeviceCreateObjects(pDevice);
    if (RT_FAILURE(rc))
        vboxDXDeviceDeleteObjects(pDevice);

    return hr;
}


void vboxDXDestroyDevice(PVBOXDX_DEVICE pDevice)
{
    /* Flush will deallocate staging resources. */
    vboxDXFlush(pDevice, true);

    PVBOXDXKMRESOURCE pKMResource, pNextKMResource;
    RTListForEachSafe(&pDevice->listResources, pKMResource, pNextKMResource, VBOXDXKMRESOURCE, nodeResource)
        vboxDXDestroyResource(pDevice, pKMResource->pResource);

    dxDestroyDeferredResources(pDevice);

    PVBOXDXSHADER pShader, pNextShader;
    RTListForEachSafe(&pDevice->listShaders, pShader, pNextShader, VBOXDXSHADER, node)
        vboxDXDestroyShader(pDevice, pShader);

    PVBOXDXQUERY pQuery, pNextQuery;
    RTListForEachSafe(&pDevice->listQueries, pQuery, pNextQuery, VBOXDXQUERY, nodeQuery)
        vboxDXDestroyQuery(pDevice, pQuery);

    PVBOXDXCOALLOCATION pCOA, pNextCOA;
    RTListForEachSafe(&pDevice->listCOAQuery, pCOA, pNextCOA, VBOXDXCOALLOCATION, nodeAllocationsChain)
        vboxDXDestroyCOAllocation(pDevice, pCOA);
    RTListForEachSafe(&pDevice->listCOAStreamOutput, pCOA, pNextCOA, VBOXDXCOALLOCATION, nodeAllocationsChain)
        vboxDXDestroyCOAllocation(pDevice, pCOA);

    if (pDevice->hShaderAllocation)
    {
        D3DDDICB_DEALLOCATE ddiDeallocate;
        RT_ZERO(ddiDeallocate);
        ddiDeallocate.NumAllocations = 1;
        ddiDeallocate.HandleList     = &pDevice->hShaderAllocation;

        HRESULT hr = pDevice->pRTCallbacks->pfnDeallocateCb(pDevice->hRTDevice.handle, &ddiDeallocate);
        LogFlowFunc(("pfnDeallocateCb returned %d", hr));
        AssertStmt(SUCCEEDED(hr), vboxDXDeviceSetError(pDevice, hr));

        pDevice->hShaderAllocation = 0;
    }

    D3DDDICB_DESTROYCONTEXT ddiDestroyContext;
    ddiDestroyContext.hContext = pDevice->hContext;

    HRESULT hr = pDevice->pRTCallbacks->pfnDestroyContextCb(pDevice->hRTDevice.handle, &ddiDestroyContext);
    LogFlowFunc(("hr %d, hContext 0x%p",  hr, pDevice->hContext)); RT_NOREF(hr);

    vboxDXDeviceDeleteObjects(pDevice);

    RTMemFree(pDevice->VideoDevice.paDecodeProfile);
    pDevice->VideoDevice.paDecodeProfile = 0;

    RTMemFree(pDevice->VideoDevice.config.pConfigInfo);
    pDevice->VideoDevice.config.pConfigInfo = 0;
}
