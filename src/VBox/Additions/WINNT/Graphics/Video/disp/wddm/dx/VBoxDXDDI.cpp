/* $Id: VBoxDXDDI.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox D3D11 user mode DDI interface.
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

#define WINAPI_FAMILY WINAPI_FAMILY_DESKTOP_APP

#include <iprt/alloc.h>
#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <VBox/log.h>

#include <iprt/win/windows.h>
#include <iprt/win/d3dkmthk.h>

#include <d3d10umddi.h>
#include <Psapi.h>

#include "VBoxDX.h"

#include <VBoxWddmUmHlp.h>

RTDECL(void) RTLogWriteUser(const char* pachChars, size_t cbChars)
{
    RT_NOREF(cbChars);

    if (pachChars)
        VBoxWddmUmLog(pachChars /*, cbChars */);
}
//RT_EXPORT_SYMBOL(RTLogWriteUser);


static uint64_t const g_aSupportedDDIInterfaceVersions[] =
{
    D3D10_0_DDI_SUPPORTED,
    D3D10_1_DDI_SUPPORTED,
    D3D11_0_DDI_SUPPORTED,
    D3D11_1_DDI_SUPPORTED
};


static bool isInterfaceSupported(UINT uInterface)
{
    for (unsigned i = 0; i < RT_ELEMENTS(g_aSupportedDDIInterfaceVersions); ++i)
    {
        uint32_t const uSupportedInterface = (uint32_t)(g_aSupportedDDIInterfaceVersions[i] >> 32);
        if (uSupportedInterface == uInterface)
            return true;
    }
    return false;
}


/*
 * Helpers.
 */

static HRESULT vboxDXQueryAdapterInfo(D3D10DDIARG_OPENADAPTER const *pOpenData, VBOXWDDM_QAI **ppAdapterInfo)
{
    VBOXWDDM_QAI *pAdapterInfo = (VBOXWDDM_QAI *)RTMemAllocZ(sizeof(VBOXWDDM_QAI));
    AssertReturn(pAdapterInfo, E_OUTOFMEMORY);

    D3DDDICB_QUERYADAPTERINFO DdiQuery;
    DdiQuery.PrivateDriverDataSize = sizeof(VBOXWDDM_QAI);
    DdiQuery.pPrivateDriverData = pAdapterInfo;
    HRESULT hr = pOpenData->pAdapterCallbacks->pfnQueryAdapterInfoCb(pOpenData->hRTAdapter.handle, &DdiQuery);
    AssertReturnStmt(SUCCEEDED(hr), RTMemFree(pAdapterInfo), hr);

    /** @todo Check that the miniport version matches display version. */
    *ppAdapterInfo = pAdapterInfo;

    return hr;
}

static HRESULT vboxDXAdapterInit(D3D10DDIARG_OPENADAPTER const* pOpenData, VBOXWDDM_QAI* pAdapterInfo,
    PVBOXDXADAPTER* ppAdapter)
{
    VBOXGAHWINFO *pHWInfo = &pAdapterInfo->u.vmsvga.HWInfo;
    if (   pHWInfo->u32HwType != VBOX_GA_HW_TYPE_VMSVGA
        || pHWInfo->u.svga.au32Caps[SVGA3D_DEVCAP_DXCONTEXT] == 0)
    {
        /* The host does not support DX. */
        AssertFailedReturn(E_FAIL);
    }

    PVBOXDXADAPTER pAdapter = (PVBOXDXADAPTER)RTMemAllocZ(sizeof(VBOXDXADAPTER));

    AssertReturn(pAdapter, E_OUTOFMEMORY);

    pAdapter->hRTAdapter = pOpenData->hRTAdapter.handle;
    pAdapter->uIfVersion = pOpenData->Interface;
    pAdapter->uRtVersion = pOpenData->Version;
    pAdapter->RtCallbacks = *pOpenData->pAdapterCallbacks;
    pAdapter->enmHwType = pAdapterInfo->enmHwType;

    pAdapter->AdapterInfo = *pAdapterInfo;
    pAdapter->f3D = true;

    pAdapter->fVBoxCaps = pHWInfo->u.svga.au32Caps[SVGA3D_DEVCAP_3D];

    *ppAdapter = pAdapter;

    return S_OK;
}


/*
 * Device functions.
 */

static void APIENTRY ddi11_1DefaultConstantBufferUpdateSubresourceUP(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDstResource, // A handle to the destination resource to copy to.
    UINT DstSubresource,             // An index that indicates the destination subresource to copy to.
    const D3D10_DDI_BOX* pDstBox,    // The region of the destination subresource to copy data to.
    const VOID* pSysMemUP,           // A pointer to the source data used to update the dest subresouce.
    UINT RowPitch,                   // The offset, in bytes, to move to the next row of source data.
    UINT DepthPitch,                 // The offset, in bytes, to move to the next depth slice of source data.
    UINT CopyFlags                   // A bitwise OR of the values in the D3D11_1_DDI_COPY_FLAGS
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pDstResource = (PVBOXDX_RESOURCE)hDstResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pDstResource 0x%p, DstSubresource %d, pDstBox %p, pSysMemUP %p, RowPitch %d, DepthPitch %d, CopyFlags 0x%x",
                 pDevice, pDstResource, DstSubresource, pDstBox, pSysMemUP, RowPitch, DepthPitch, CopyFlags));

    vboxDXResourceUpdateSubresourceUP(pDevice, pDstResource, DstSubresource, pDstBox, pSysMemUP, RowPitch, DepthPitch, CopyFlags);
}

static void APIENTRY ddi10DefaultConstantBufferUpdateSubresourceUP(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDstResource, // A handle to the destination resource to copy to.
    UINT DstSubresource,             // An index that indicates the destination subresource to copy to.
    const D3D10_DDI_BOX* pDstBox,    // The region of the destination subresource to copy data to.
    const VOID* pSysMemUP,           // A pointer to the source data used to update the dest subresouce.
    UINT RowPitch,                   // The offset, in bytes, to move to the next row of source data.
    UINT DepthPitch                  // The offset, in bytes, to move to the next depth slice of source data.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pDstResource = (PVBOXDX_RESOURCE)hDstResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pDstResource 0x%p, DstSubresource %d, pDstBox %p, pSysMemUP %p, RowPitch %d, DepthPitch %d",
                 pDevice, pDstResource, DstSubresource, pDstBox, pSysMemUP, RowPitch, DepthPitch));

    vboxDXResourceUpdateSubresourceUP(pDevice, pDstResource, DstSubresource, pDstBox, pSysMemUP, RowPitch, DepthPitch, 0);
}

static void APIENTRY ddi11_1VsSetConstantBuffers(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot,             // The starting constant buffer to set.
    UINT NumBuffers,            // The total number of buffers to set.
    const D3D10DDI_HRESOURCE* phBuffers, // An array of handles to the constant buffers, beginning with the buffer that StartBuffer specifies.
    const UINT* pFirstConstant, // A pointer to the first constant in the buffer pointed to by StartBuffer.
    const UINT* pNumConstants   // The number of constants in the buffer pointed to by StartBuffer.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumBuffers = %u\n", pDevice, StartSlot, NumBuffers));

    vboxDXSetConstantBuffers(pDevice, SVGA3D_SHADERTYPE_VS, StartSlot, NumBuffers, (PVBOXDX_RESOURCE *)phBuffers, pFirstConstant, pNumConstants);
}

static void APIENTRY ddi10VsSetConstantBuffers(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot,             // The starting constant buffer to set.
    UINT NumBuffers,            // The total number of buffers to set.
    const D3D10DDI_HRESOURCE* phBuffers // An array of handles to the constant buffers, beginning with the buffer that StartBuffer specifies.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumBuffers = %u\n", pDevice, StartSlot, NumBuffers));

    vboxDXSetConstantBuffers(pDevice, SVGA3D_SHADERTYPE_VS, StartSlot, NumBuffers, (PVBOXDX_RESOURCE *)phBuffers, NULL, NULL);
}

static void APIENTRY ddi10PsSetShaderResources(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot, // The offset to the first view to set.
    UINT NumViews,  // The total number of views to set.
    const D3D10DDI_HSHADERRESOURCEVIEW* phShaderResourceViews // An array of handles to the shader resource views
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumViews = %u\n", pDevice, StartSlot, NumViews));

    vboxDXSetShaderResourceViews(pDevice, SVGA3D_SHADERTYPE_PS, StartSlot, NumViews, (PVBOXDXSHADERRESOURCEVIEW *)phShaderResourceViews);
}

static void APIENTRY ddi10PsSetShader(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADER hShader  // A handle to the pixel shader code object.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADER pShader = (PVBOXDXSHADER)hShader.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShader 0x%p", pDevice, pShader));

    vboxDXSetShader(pDevice, SVGA3D_SHADERTYPE_PS, pShader);
}

static void APIENTRY ddi10PsSetSamplers(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot,   // The offset to the first sampler to set.
    UINT NumSamplers, // The total number of samplers to set.
    const D3D10DDI_HSAMPLER* phSamplers // An array of handles to the samplers.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumSamplers = %u\n", pDevice, StartSlot, NumSamplers));

    Assert(NumSamplers <= SVGA3D_DX_MAX_SAMPLERS);
    NumSamplers = RT_MIN(NumSamplers, SVGA3D_DX_MAX_SAMPLERS);

    /* Fetch sampler ids. */
    uint32_t aSamplerIds[SVGA3D_DX_MAX_SAMPLERS];
    for (unsigned i = 0; i < NumSamplers; ++i)
    {
        VBOXDX_SAMPLER_STATE *pSamplerState = (PVBOXDX_SAMPLER_STATE)phSamplers[i].pDrvPrivate;
        aSamplerIds[i] = pSamplerState ? pSamplerState->uSamplerId : SVGA3D_INVALID_ID;
    }

    vboxDXSetSamplers(pDevice, SVGA3D_SHADERTYPE_PS, StartSlot, NumSamplers, aSamplerIds);
}

static void APIENTRY ddi10VsSetShader(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADER hShader // A handle to the vertex shader code object.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADER pShader = (PVBOXDXSHADER)hShader.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShader 0x%p", pDevice, pShader));

    vboxDXSetShader(pDevice, SVGA3D_SHADERTYPE_VS, pShader);
}

static void APIENTRY ddi10DrawIndexed(
    D3D10DDI_HDEVICE hDevice,
    UINT IndexCount, // The number of indexes in the index buffer that indexes are read from to draw the primitives.
    UINT StartIndexLocation, // The first index in the index buffer that indexes are read from to draw the primitives.
    INT BaseVertexLocation // The number that should be added to each index that is referenced by the various primitives
                           // to determine the actual index of the vertex elements in each vertex stream.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartIndexLocation = %u, BaseVertexLocation = %u, IndexCount = %u\n", pDevice, StartIndexLocation, BaseVertexLocation, IndexCount));

    vboxDXDrawIndexed(pDevice, IndexCount, StartIndexLocation, BaseVertexLocation);
}

static void APIENTRY ddi10Draw(
    D3D10DDI_HDEVICE hDevice,
    UINT VertexCount,        // The number of vertices in the vertex buffer to draw the primitives.
    UINT StartVertexLocation // The first vertex in the vertex buffer to draw the primitives.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, VertexCount = %u, StartVertexLocation = %u\n", pDevice, VertexCount, StartVertexLocation));

    vboxDXDraw(pDevice, VertexCount, StartVertexLocation);
}

static void APIENTRY ddi10DynamicIABufferMapNoOverwrite(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource, // A handle to the resource to map.
    UINT Subresource,             // An index that indicates the subresource to map.
    D3D10_DDI_MAP DDIMap,         // A D3D10_DDI_MAP-typed value that indicates the access level to map the subresource to.
    UINT Flags,                   // A D3D10_DDI_MAP_FLAG-typed value that indicates how to map the subresource.
    D3D10DDI_MAPPED_SUBRESOURCE* pMappedSubResource // A pointer to a D3D10DDI_MAPPED_SUBRESOURCE structure that receives the information about the mapped subresource.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)hResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pResource 0x%p, subres %d, map %d, flags 0x%X", pDevice, pResource, Subresource, DDIMap, Flags));

    vboxDXResourceMap(pDevice, pResource, Subresource, DDIMap, Flags, pMappedSubResource);
}

static void APIENTRY ddi10DynamicIABufferUnmap(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource, // A handle to the resource to unmap.
    UINT Subresource              // An index that indicates the subresource to unmap.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)hResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pResource 0x%p, subres %d", pDevice, pResource, Subresource));

    vboxDXResourceUnmap(pDevice, pResource, Subresource);
}

static void APIENTRY ddi10DynamicConstantBufferMapDiscard(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource, // A handle to the resource to map.
    UINT Subresource,             // An index that indicates the subresource to map.
    D3D10_DDI_MAP DDIMap,         // A D3D10_DDI_MAP-typed value that indicates the access level to map the subresource to.
    UINT Flags,                   // A D3D10_DDI_MAP_FLAG-typed value that indicates how to map the subresource.
    D3D10DDI_MAPPED_SUBRESOURCE* pMappedSubResource // A pointer to a D3D10DDI_MAPPED_SUBRESOURCE structure that receives the information about the mapped subresource.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)hResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pResource 0x%p, subres %d, map %d, flags 0x%X", pDevice, pResource, Subresource, DDIMap, Flags));

    vboxDXResourceMap(pDevice, pResource, Subresource, DDIMap, Flags, pMappedSubResource);
}

static void APIENTRY ddi10DynamicIABufferMapDiscard(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource, // A handle to the resource to map.
    UINT Subresource,             // An index that indicates the subresource to map.
    D3D10_DDI_MAP DDIMap,         // A D3D10_DDI_MAP-typed value that indicates the access level to map the subresource to.
    UINT Flags,                   // A D3D10_DDI_MAP_FLAG-typed value that indicates how to map the subresource.
    D3D10DDI_MAPPED_SUBRESOURCE* pMappedSubResource // A pointer to a D3D10DDI_MAPPED_SUBRESOURCE structure that receives the information about the mapped subresource.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)hResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pResource 0x%p, subres %d, map %d, flags 0x%X", pDevice, pResource, Subresource, DDIMap, Flags));

    vboxDXResourceMap(pDevice, pResource, Subresource, DDIMap, Flags, pMappedSubResource);
}

static void APIENTRY ddi10DynamicConstantBufferUnmap(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource, // A handle to the resource to unmap.
    UINT Subresource              // An index that indicates the subresource to unmap.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)hResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pResource 0x%p, subres %d", pDevice, pResource, Subresource));

    vboxDXResourceUnmap(pDevice, pResource, Subresource);
}

static void APIENTRY ddi11_1PsSetConstantBuffers(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot,  // The starting constant buffer to set.
    UINT NumBuffers, // The total number of buffers to set.
    const D3D10DDI_HRESOURCE* phBuffers, // An array of handles to the constant buffers.
    const UINT* pFirstConstant, // A pointer to the first constant in the buffer pointed to by StartBuffer.
    const UINT* pNumConstants   // The number of constants in the buffer pointed to by StartBuffer.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumBuffers = %u\n", pDevice, StartSlot, NumBuffers));

    vboxDXSetConstantBuffers(pDevice, SVGA3D_SHADERTYPE_PS, StartSlot, NumBuffers, (PVBOXDX_RESOURCE *)phBuffers, pFirstConstant, pNumConstants);
}

static void APIENTRY ddi10PsSetConstantBuffers(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot,             // The starting constant buffer to set.
    UINT NumBuffers,            // The total number of buffers to set.
    const D3D10DDI_HRESOURCE* phBuffers // An array of handles to the constant buffers, beginning with the buffer that StartBuffer specifies.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumBuffers = %u\n", pDevice, StartSlot, NumBuffers));

    vboxDXSetConstantBuffers(pDevice, SVGA3D_SHADERTYPE_PS, StartSlot, NumBuffers, (PVBOXDX_RESOURCE *)phBuffers, NULL, NULL);
}

static void APIENTRY ddi10IaSetInputLayout(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HELEMENTLAYOUT hInputLayout // A handle to the input layout object.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXELEMENTLAYOUT pInputLayout = (PVBOXDXELEMENTLAYOUT)hInputLayout.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pInputLayout 0x%p", pDevice, pInputLayout));

    vboxDXSetInputLayout(pDevice, pInputLayout);
}

static void APIENTRY ddi10IaSetVertexBuffers(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot,  // The starting vertex buffer to set.
    UINT NumBuffers, // The total number of buffers to set.
    const D3D10DDI_HRESOURCE* phBuffers, // An array of handles to the vertex buffers, beginning with the buffer that StartBuffer specifies.
    const UINT* pStrides, // An array of values that indicate the sizes, in bytes, from one vertex to the next vertex for each buffer
    const UINT* pOffsets  // An array of values that indicate the offsets, in bytes, into each vertex buffer.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumBuffers = %u\n", pDevice, StartSlot, NumBuffers));

    vboxDXSetVertexBuffers(pDevice, StartSlot, NumBuffers, (PVBOXDX_RESOURCE *)phBuffers, pStrides, pOffsets);
}

static void APIENTRY ddi10IaSetIndexBuffer(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hBuffer, // A handle to the index buffer to set.
    DXGI_FORMAT Format,  // A DXGI_FORMAT-typed value that indicates the pixel format of the index buffer.
                        // Only the DXGI_FORMAT_R16_UINT and DXGI_FORMAT_R32_UINT formats are valid.
    UINT Offset         // The offset, in bytes, into the index buffer.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, Format = %u, Offset = %u\n", pDevice, Format, Offset));

    vboxDXSetIndexBuffer(pDevice, (PVBOXDX_RESOURCE)hBuffer.pDrvPrivate, Format, Offset);
}

static void APIENTRY ddi10DrawIndexedInstanced(
    D3D10DDI_HDEVICE hDevice,
    UINT IndexCountPerInstance, // The number of indexes per instance.
    UINT InstanceCount,         // The number of instances of the index buffer that indexes are read from.
    UINT StartIndexLocation,    // The first index in the index buffer that indexes are read from to draw the primitives.
    INT BaseVertexLocation,     // The number that should be added to each index that is referenced by the various primitives to determine
                                // the actual index of the vertex elements in each vertex stream.
    UINT StartInstanceLocation  // The first instance of the index buffer that indexes are read from to draw the primitives.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, IndexCountPerInstance = %u, InstanceCount = %u, StartIndexLocation = %u, BaseVertexLocation = %u, StartInstanceLocation = %u\n", pDevice, IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation));

    vboxDXDrawIndexedInstanced(pDevice, IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
}

static void APIENTRY ddi10DrawInstanced(
    D3D10DDI_HDEVICE hDevice,
    UINT VertexCountPerInstance, // The number of vertices per instance of the buffer that vertices are read from to draw the primitives.
    UINT InstanceCount,          // The number of instances of the buffer that vertices are read from to draw the primitives.
    UINT StartVertexLocation,    // The first vertex in the buffer that vertices are read from to draw the primitives.
    UINT StartInstanceLocation   // The first instance of the buffer that vertices are read from to draw the primitives.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, VertexCountPerInstance = %u, InstanceCount = %u, StartVertexLocation = %u, StartInstanceLocation = %u\n", pDevice, VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation));

    vboxDXDrawInstanced(pDevice, VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
}

static void APIENTRY ddi10DynamicResourceMapDiscard(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource, // A handle to the resource to map.
    UINT Subresource,             // An index that indicates the subresource to map.
    D3D10_DDI_MAP DDIMap,         // A D3D10_DDI_MAP-typed value that indicates the access level to map the subresource to.
    UINT Flags,                   // A D3D10_DDI_MAP_FLAG-typed value that indicates how to map the subresource.
    D3D10DDI_MAPPED_SUBRESOURCE* pMappedSubResource // A pointer to a D3D10DDI_MAPPED_SUBRESOURCE structure that receives the information about the mapped subresource.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)hResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pResource 0x%p, subres %d, map %d, flags 0x%X", pDevice, pResource, Subresource, DDIMap, Flags));

    vboxDXResourceMap(pDevice, pResource, Subresource, DDIMap, Flags, pMappedSubResource);
}

static void APIENTRY ddi10DynamicResourceUnmap(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource, // A handle to the resource to unmap.
    UINT Subresource              // An index that indicates the subresource to unmap.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)hResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pResource 0x%p, subres %d", pDevice, pResource, Subresource));

    vboxDXResourceUnmap(pDevice, pResource, Subresource);
}

static void APIENTRY ddi11_1GsSetConstantBuffers(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot,  // The starting constant buffer to set.
    UINT NumBuffers, // The total number of buffers to set.
    const D3D10DDI_HRESOURCE* phBuffers, // An array of handles to the constant buffers, beginning with the buffer that StartBuffer specifies.
    const UINT* pFirstConstant, // A pointer to the first constant in the buffer pointed to by StartBuffer.
    const UINT* pNumConstants   // The number of constants in the buffer pointed to by StartBuffer.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumBuffers = %u\n", pDevice, StartSlot, NumBuffers));

    vboxDXSetConstantBuffers(pDevice, SVGA3D_SHADERTYPE_GS, StartSlot, NumBuffers, (PVBOXDX_RESOURCE *)phBuffers, pFirstConstant, pNumConstants);
}

static void APIENTRY ddi10GsSetConstantBuffers(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot,             // The starting constant buffer to set.
    UINT NumBuffers,            // The total number of buffers to set.
    const D3D10DDI_HRESOURCE* phBuffers // An array of handles to the constant buffers, beginning with the buffer that StartBuffer specifies.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumBuffers = %u\n", pDevice, StartSlot, NumBuffers));

    vboxDXSetConstantBuffers(pDevice, SVGA3D_SHADERTYPE_GS, StartSlot, NumBuffers, (PVBOXDX_RESOURCE *)phBuffers, NULL, NULL);
}

static void APIENTRY ddi10GsSetShader(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADER hShader // A handle to the shader code object.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADER pShader = (PVBOXDXSHADER)hShader.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShader 0x%p", pDevice, pShader));

    vboxDXSetShader(pDevice, SVGA3D_SHADERTYPE_GS, pShader);
}

static void APIENTRY ddi10IaSetTopology(
    D3D10DDI_HDEVICE hDevice,
    D3D10_DDI_PRIMITIVE_TOPOLOGY PrimitiveTopology // The primitive topology to set.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, PrimitiveTopology = %u\n", pDevice, PrimitiveTopology));

    if (PrimitiveTopology == D3D10_DDI_PRIMITIVE_TOPOLOGY_UNDEFINED)
        return;

    vboxDXIaSetTopology(pDevice, PrimitiveTopology);
}

static void APIENTRY ddi10StagingResourceMap(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource, // A handle to the resource to map.
    UINT Subresource,             // An index that indicates the subresource to map.
    D3D10_DDI_MAP DDIMap,         // A D3D10_DDI_MAP-typed value that indicates the access level to map the subresource to.
    UINT Flags,                   // A D3D10_DDI_MAP_FLAG-typed value that indicates how to map the subresource.
    D3D10DDI_MAPPED_SUBRESOURCE* pMappedSubResource // A pointer to a D3D10DDI_MAPPED_SUBRESOURCE structure that receives the information about the mapped subresource.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)hResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pResource 0x%p, subres %d, map %d, flags 0x%X", pDevice, pResource, Subresource, DDIMap, Flags));

    vboxDXResourceMap(pDevice, pResource, Subresource, DDIMap, Flags, pMappedSubResource);
}

static void APIENTRY ddi10StagingResourceUnmap(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource, // A handle to the resource to unmap.
    UINT Subresource              // An index that indicates the subresource to unmap.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)hResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pResource 0x%p, subres %d", pDevice, pResource, Subresource));

    vboxDXResourceUnmap(pDevice, pResource, Subresource);
}

static void APIENTRY ddi10VsSetShaderResources(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot, // The offset to the first view to set.
    UINT NumViews,  // The total number of views to set.
    const D3D10DDI_HSHADERRESOURCEVIEW* phShaderResourceViews // An array of handles to the shader resource views
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumViews = %u\n", pDevice, StartSlot, NumViews));

    vboxDXSetShaderResourceViews(pDevice, SVGA3D_SHADERTYPE_VS, StartSlot, NumViews, (PVBOXDXSHADERRESOURCEVIEW *)phShaderResourceViews);
}

static void APIENTRY ddi10VsSetSamplers(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot,   // The offset to the first sampler to set.
    UINT NumSamplers, // The total number of samplers to set.
    const D3D10DDI_HSAMPLER* phSamplers // An array of handles to the samplers.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumSamplers = %u\n", pDevice, StartSlot, NumSamplers));

    Assert(NumSamplers <= SVGA3D_DX_MAX_SAMPLERS);
    NumSamplers = RT_MIN(NumSamplers, SVGA3D_DX_MAX_SAMPLERS);

    /* Fetch sampler ids. */
    uint32_t aSamplerIds[SVGA3D_DX_MAX_SAMPLERS];
    for (unsigned i = 0; i < NumSamplers; ++i)
    {
        VBOXDX_SAMPLER_STATE *pSamplerState = (PVBOXDX_SAMPLER_STATE)phSamplers[i].pDrvPrivate;
        aSamplerIds[i] = pSamplerState ? pSamplerState->uSamplerId : SVGA3D_INVALID_ID;
    }

    vboxDXSetSamplers(pDevice, SVGA3D_SHADERTYPE_VS, StartSlot, NumSamplers, aSamplerIds);
}

static void APIENTRY ddi10GsSetShaderResources(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot, // The offset to the first view to set.
    UINT NumViews,  // The total number of views to set.
    const D3D10DDI_HSHADERRESOURCEVIEW* phShaderResourceViews // An array of handles to the shader resource views
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumViews = %u\n", pDevice, StartSlot, NumViews));

    vboxDXSetShaderResourceViews(pDevice, SVGA3D_SHADERTYPE_GS, StartSlot, NumViews, (PVBOXDXSHADERRESOURCEVIEW *)phShaderResourceViews);
}

static void APIENTRY ddi10GsSetSamplers(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot,   // The offset to the first sampler to set.
    UINT NumSamplers, // The total number of samplers to set.
    const D3D10DDI_HSAMPLER* phSamplers // An array of handles to the samplers.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumSamplers = %u\n", pDevice, StartSlot, NumSamplers));

    Assert(NumSamplers <= SVGA3D_DX_MAX_SAMPLERS);
    NumSamplers = RT_MIN(NumSamplers, SVGA3D_DX_MAX_SAMPLERS);

    /* Fetch sampler ids. */
    uint32_t aSamplerIds[SVGA3D_DX_MAX_SAMPLERS];
    for (unsigned i = 0; i < NumSamplers; ++i)
    {
        VBOXDX_SAMPLER_STATE *pSamplerState = (PVBOXDX_SAMPLER_STATE)phSamplers[i].pDrvPrivate;
        aSamplerIds[i] = pSamplerState ? pSamplerState->uSamplerId : SVGA3D_INVALID_ID;
    }

    vboxDXSetSamplers(pDevice, SVGA3D_SHADERTYPE_GS, StartSlot, NumSamplers, aSamplerIds);
}

static void APIENTRY ddi11SetRenderTargets(
    D3D10DDI_HDEVICE hDevice,
    const D3D10DDI_HRENDERTARGETVIEW* phRenderTargetView, // An array of handles to the render target view (RTV) objects to set. Note that some handle values can be NULL.
    UINT NumRTVs,    // The number of elements in the array provided in phRenderTargetView for the RTVs to set.
    UINT ClearSlots, // The number of RTV objects to unbind; that is, those render target view objects that were previously bound but no longer need to be bound.
    D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView,               // A handle to the depth-stencil buffer to set.
    const D3D11DDI_HUNORDEREDACCESSVIEW* phUnorderedAccessView, // An array of handles to the unordered access view (UAV) objects to set.
    const UINT* pUAVInitialCounts, // An array of appendand consume buffer offsets.
    UINT UAVStartSlot,  // Index of the first UAV to bind. UAVStartSlot must be at least as great as the NumRTVs parameter.
    UINT NumUAVs,       // The number of UAVs to bind.
    UINT UAVRangeStart, // The first UAV in the set of all updated UAVs (which includes NULL bindings).
    UINT UAVRangeSize   // The number of UAVs in the set of all updated UAVs (which includes NULL bindings).
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice %p, NumRTVs %u, ClearSlots %u, UAVStartSlot %u, NumUAVs %u, UAVRangeStart %u, UAVRangeSize %u\n",
                 pDevice, NumRTVs, ClearSlots, UAVStartSlot, NumUAVs, UAVRangeStart, UAVRangeSize));

    AssertReturnVoid(   NumRTVs <= SVGA3D_MAX_SIMULTANEOUS_RENDER_TARGETS
                     && ClearSlots <= SVGA3D_MAX_SIMULTANEOUS_RENDER_TARGETS
                     && NumRTVs + ClearSlots <= SVGA3D_MAX_SIMULTANEOUS_RENDER_TARGETS);

    /* UAVs take slots right after render targets.
     *
     * For example Windows calls this as:
     *   NumRTVs = 0, ClearSlots = 0, UAVStartSlot = 0, NumUAVs = 1,
     * even if there was a render target at slot 0 already.
     * And then:
     *   NumRTVs 3, ClearSlots 0, UAVStartSlot 3, NumUAVs 0
     *   NumRTVs 1, ClearSlots 2, UAVStartSlot 3, NumUAVs 0
     *   NumRTVs 1, ClearSlots 0, UAVStartSlot 3, NumUAVs 0
     *
     * There are 2 separate commands (SetRenderTargets and SetUnorderedAccessViews) for this one operation.
     *
     * SetRenderTargets: clear all slots of previously set render targets to make free slots for UAVs.
     * SetUnorderedAccessViews: always send the command.
     */
    if (NumUAVs)
        ClearSlots = RT_MAX(ClearSlots, pDevice->pipeline.cRenderTargetViews - NumRTVs);

    PVBOXDXDEPTHSTENCILVIEW pDepthStencilView = (PVBOXDXDEPTHSTENCILVIEW)hDepthStencilView.pDrvPrivate;

    vboxDXSetRenderTargets(pDevice, pDepthStencilView, NumRTVs, ClearSlots, (PVBOXDXRENDERTARGETVIEW *)phRenderTargetView);

    AssertReturnVoidStmt(   NumUAVs <= D3D11_1_UAV_SLOT_COUNT
                         && UAVStartSlot <= SVGA3D_MAX_SIMULTANEOUS_RENDER_TARGETS,
                         vboxDXDeviceSetError(pDevice, E_INVALIDARG));

    vboxDXSetUnorderedAccessViews(pDevice, UAVStartSlot, NumUAVs, (PVBOXDXUNORDEREDACCESSVIEW *)phUnorderedAccessView, pUAVInitialCounts);

    RT_NOREF(UAVRangeStart, UAVRangeSize); /* These are hints and not used by the driver. */
}

static void APIENTRY ddi10SetRenderTargets(
    D3D10DDI_HDEVICE hDevice,
    const D3D10DDI_HRENDERTARGETVIEW* phRenderTargetView, // An array of handles to the render target view (RTV) objects to set. Note that some handle values can be NULL.
    UINT NumRTVs,    // The number of elements in the array provided in phRenderTargetView for the RTVs to set.
    UINT ClearSlots, // The number of RTV objects to unbind; that is, those render target view objects that were previously bound but no longer need to be bound.
    D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView               // A handle to the depth-stencil buffer to set.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice %p, NumRTVs %u, ClearSlots %u\n",
                 pDevice, NumRTVs, ClearSlots));

    AssertReturnVoid(   NumRTVs <= SVGA3D_MAX_SIMULTANEOUS_RENDER_TARGETS
                     && ClearSlots <= SVGA3D_MAX_SIMULTANEOUS_RENDER_TARGETS
                     && NumRTVs + ClearSlots <= SVGA3D_MAX_SIMULTANEOUS_RENDER_TARGETS);

    PVBOXDXDEPTHSTENCILVIEW pDepthStencilView = (PVBOXDXDEPTHSTENCILVIEW)hDepthStencilView.pDrvPrivate;

    vboxDXSetRenderTargets(pDevice, pDepthStencilView, NumRTVs, ClearSlots, (PVBOXDXRENDERTARGETVIEW *)phRenderTargetView);
}

static void APIENTRY ddi10ShaderResourceViewReadAfterWriteHazard(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADERRESOURCEVIEW hResource, // A handle to the resource.
    D3D10DDI_HRESOURCE hShaderResourceView  // A handle to the driver's private data for a shader resource view object.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)hResource.pDrvPrivate;
    PVBOXDXSHADERRESOURCEVIEW pShaderResourceView = (PVBOXDXSHADERRESOURCEVIEW)hShaderResourceView.pDrvPrivate;
    LogFlowFunc(("pDevice %p, pResource %p, pShaderResourceView %p\n", pDevice, pResource, pShaderResourceView));
    RT_NOREF(pDevice, pResource, pShaderResourceView);
}

static void APIENTRY ddi10ResourceReadAfterWriteHazard(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource // A handle to the resource.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)hResource.pDrvPrivate;
    LogFlowFunc(("pDevice %p, pResource %p\n", pDevice, pResource));
    RT_NOREF(pDevice, pResource);
}

static void APIENTRY ddi10SetBlendState(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HBLENDSTATE hBlendState, // A handle to the blend state to set.
    const FLOAT BlendFactor[4],  // Array of blend factors, one for each RGBA component.
    UINT SampleMask              // A sample format mask.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_BLENDSTATE pBlendState = (PVBOXDX_BLENDSTATE)hBlendState.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pBlendState 0x%p, BlendFactor (" FLOAT_FMT_STR ", " FLOAT_FMT_STR ", " FLOAT_FMT_STR ", " FLOAT_FMT_STR "), SampleMask 0x%x",
                 pDevice, pBlendState, FLOAT_FMT_ARGS(BlendFactor[0]), FLOAT_FMT_ARGS(BlendFactor[1]), FLOAT_FMT_ARGS(BlendFactor[2]), FLOAT_FMT_ARGS(BlendFactor[3]), SampleMask));

#ifdef LOG_ENABLED
    if (pBlendState)
    {
        LogFlowFunc(("[%u] ATCE %d, IBE %d, RT: b %d, lo %d, src %d, dest %d. op %d, asrc %d, adest %d. aop %d, lo %d, m %x\n",
                     pBlendState->uBlendId, pBlendState->BlendDesc.AlphaToCoverageEnable, pBlendState->BlendDesc.IndependentBlendEnable,
                     pBlendState->BlendDesc.RenderTarget[0].BlendEnable,
                     pBlendState->BlendDesc.RenderTarget[0].LogicOpEnable,
                     pBlendState->BlendDesc.RenderTarget[0].SrcBlend,
                     pBlendState->BlendDesc.RenderTarget[0].DestBlend,
                     pBlendState->BlendDesc.RenderTarget[0].BlendOp,
                     pBlendState->BlendDesc.RenderTarget[0].SrcBlendAlpha,
                     pBlendState->BlendDesc.RenderTarget[0].DestBlendAlpha,
                     pBlendState->BlendDesc.RenderTarget[0].BlendOpAlpha,
                     pBlendState->BlendDesc.RenderTarget[0].LogicOp,
                     pBlendState->BlendDesc.RenderTarget[0].RenderTargetWriteMask));
    }
#endif


    vboxDXSetBlendState(pDevice, pBlendState, BlendFactor, SampleMask);
}

static void APIENTRY ddi10SetDepthStencilState(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HDEPTHSTENCILSTATE hDepthStencilState, // A handle to the depth-stencil state to set.
    UINT StencilRef                     // A stencil reference value to compare against.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_DEPTHSTENCIL_STATE pDepthStencilState = (PVBOXDX_DEPTHSTENCIL_STATE)hDepthStencilState.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pDepthStencilState 0x%p, StencilRef %d", pDevice, pDepthStencilState, StencilRef));

    vboxDXSetDepthStencilState(pDevice, pDepthStencilState, StencilRef);
}

static void APIENTRY ddi10SetRasterizerState(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRASTERIZERSTATE hRasterizerState // A handle to the rasterizer state object.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RASTERIZER_STATE pRasterizerState = (PVBOXDX_RASTERIZER_STATE)hRasterizerState.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pRasterizerState 0x%p", pDevice, pRasterizerState));

    vboxDXSetRasterizerState(pDevice, pRasterizerState);
}

static void APIENTRY ddi10QueryEnd(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HQUERY  hQuery   // A handle to the query object to end.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXQUERY pQuery = (PVBOXDXQUERY)hQuery.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pQuery 0x%p", pDevice, pQuery));

    vboxDXQueryEnd(pDevice, pQuery);
}

static void APIENTRY ddi10QueryBegin(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HQUERY hQuery   // A handle to the query object to begin.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXQUERY pQuery = (PVBOXDXQUERY)hQuery.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pQuery 0x%p", pDevice, pQuery));

    vboxDXQueryBegin(pDevice, pQuery);
}

static void APIENTRY ddi11_1ResourceCopyRegion(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDstResource, // A handle to the destination resource to copy to.
    UINT DstSubresource, // An index that indicates the destination subresource to copy to.
    UINT DstX, // The x-coordinate of the destination subresource.
    UINT DstY, // The y-coordinate of the destination subresource. For one-dimensional (1-D) subresources, DstY is set to zero.
    UINT DstZ, // The z-coordinate of the destination subresource. For one-dimensional (1-D) and two-dimensional (2-D) subresources, DstZ is set to zero.
    D3D10DDI_HRESOURCE hSrcResource, // A handle to the source resource to copy from.
    UINT SrcSubresource, // An index that indicates the source subresource to copy from.
    const D3D10_DDI_BOX* pSrcBox, // A pointer to a D3D10_DDI_BOX structure that specifies a box that fits on either the source or destination subresource.
                                  // If pSrcBox is NULL, the driver should copy the entire source subresouce to the destination.
    UINT CopyFlags // A value that specifies characteristics of copy operation as a bitwise OR of the values in the D3D11_1_DDI_COPY_FLAGS enumeration type.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pDstResource = (PVBOXDX_RESOURCE)hDstResource.pDrvPrivate;
    PVBOXDX_RESOURCE pSrcResource = (PVBOXDX_RESOURCE)hSrcResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pDstResource 0x%p, DstSubresource %d, Dst %d,%d,%d, pSrcResource 0x%p, SrcSubresource %d, pSrcBox %p, CopyFlags 0x%x",
                 pDevice, pDstResource, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource, pSrcBox, CopyFlags));

    vboxDXResourceCopyRegion(pDevice, pDstResource, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource, pSrcBox, CopyFlags);
}

static void APIENTRY ddi10ResourceCopyRegion(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDstResource, // A handle to the destination resource to copy to.
    UINT DstSubresource, // An index that indicates the destination subresource to copy to.
    UINT DstX, // The x-coordinate of the destination subresource.
    UINT DstY, // The y-coordinate of the destination subresource. For one-dimensional (1-D) subresources, DstY is set to zero.
    UINT DstZ, // The z-coordinate of the destination subresource. For one-dimensional (1-D) and two-dimensional (2-D) subresources, DstZ is set to zero.
    D3D10DDI_HRESOURCE hSrcResource, // A handle to the source resource to copy from.
    UINT SrcSubresource, // An index that indicates the source subresource to copy from.
    const D3D10_DDI_BOX* pSrcBox  // A pointer to a D3D10_DDI_BOX structure that specifies a box that fits on either the source or destination subresource.
                                  // If pSrcBox is NULL, the driver should copy the entire source subresouce to the destination.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pDstResource = (PVBOXDX_RESOURCE)hDstResource.pDrvPrivate;
    PVBOXDX_RESOURCE pSrcResource = (PVBOXDX_RESOURCE)hSrcResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pDstResource 0x%p, DstSubresource %d, Dst %d,%d,%d, pSrcResource 0x%p, SrcSubresource %d, pSrcBox %p",
                 pDevice, pDstResource, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource, pSrcBox));

    vboxDXResourceCopyRegion(pDevice, pDstResource, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource, pSrcBox, 0);
}

static void APIENTRY ddi11_1ResourceUpdateSubresourceUP(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDstResource, // A handle to the destination resource to copy to.
    UINT DstSubresource,             // An index that indicates the destination subresource to copy to.
    const D3D10_DDI_BOX* pDstBox,    // The region of the destination subresource to copy data to.
    const VOID* pSysMemUP,           // A pointer to the source data used to update the dest subresouce.
    UINT RowPitch,                   // The offset, in bytes, to move to the next row of source data.
    UINT DepthPitch,                 // The offset, in bytes, to move to the next depth slice of source data.
    UINT CopyFlags                   // A bitwise OR of the values in the D3D11_1_DDI_COPY_FLAGS
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pDstResource = (PVBOXDX_RESOURCE)hDstResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pDstResource 0x%p, DstSubresource %d, pDstBox %p, pSysMemUP %p, RowPitch %d, DepthPitch %d, CopyFlags 0x%x",
                 pDevice, pDstResource, DstSubresource, pDstBox, pSysMemUP, RowPitch, DepthPitch, CopyFlags));

    vboxDXResourceUpdateSubresourceUP(pDevice, pDstResource, DstSubresource, pDstBox, pSysMemUP, RowPitch, DepthPitch, CopyFlags);
}

static void APIENTRY ddi10ResourceUpdateSubresourceUP(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDstResource, // A handle to the destination resource to copy to.
    UINT DstSubresource,             // An index that indicates the destination subresource to copy to.
    const D3D10_DDI_BOX* pDstBox,    // The region of the destination subresource to copy data to.
    const VOID* pSysMemUP,           // A pointer to the source data used to update the dest subresouce.
    UINT RowPitch,                   // The offset, in bytes, to move to the next row of source data.
    UINT DepthPitch                  // The offset, in bytes, to move to the next depth slice of source data.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pDstResource = (PVBOXDX_RESOURCE)hDstResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pDstResource 0x%p, DstSubresource %d, pDstBox %p, pSysMemUP %p, RowPitch %d, DepthPitch %d",
                 pDevice, pDstResource, DstSubresource, pDstBox, pSysMemUP, RowPitch, DepthPitch));

    vboxDXResourceUpdateSubresourceUP(pDevice, pDstResource, DstSubresource, pDstBox, pSysMemUP, RowPitch, DepthPitch, 0);
}

static void APIENTRY ddi10SoSetTargets(
    D3D10DDI_HDEVICE hDevice,
    UINT NumBuffers,   // The number of elements in the array that phResource specifies.
    UINT ClearTargets, // The number of handles to stream output target resources that represents the difference between
                       // the previous number of stream output target resources (before the Microsoft Direct3D runtime calls SoSetTargets) and
                       // the new number of stream output target resources.
    const D3D10DDI_HRESOURCE* phResource, // An array of handles to the stream output target resources to set.Note that some handle values can be NULL.
    const UINT* pOffsets // An array of offsets, in bytes, into the stream output target resources in the array that phResource specifies.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, NumBuffers = %u, ClearTargets = %u\n", pDevice, NumBuffers, ClearTargets));

    AssertReturnVoid(NumBuffers <= SVGA3D_DX_MAX_SOTARGETS && ClearTargets <= SVGA3D_DX_MAX_SOTARGETS);

    uint32_t NumTargets = NumBuffers + ClearTargets;
    Assert(NumTargets <= SVGA3D_DX_MAX_SOTARGETS);
    NumTargets = RT_MIN(NumTargets, SVGA3D_DX_MAX_SOTARGETS);

    /* Fetch allocation handles. */
    D3DKMT_HANDLE aAllocations[SVGA3D_DX_MAX_SOTARGETS];
    uint32_t aOffsets[SVGA3D_DX_MAX_SOTARGETS];
    uint32_t aSizes[SVGA3D_DX_MAX_SOTARGETS];
    for (unsigned i = 0; i < NumTargets; ++i)
    {
        if (i < NumBuffers)
        {
            VBOXDX_RESOURCE *pResource = (PVBOXDX_RESOURCE)phResource[i].pDrvPrivate;
            aAllocations[i] = vboxDXGetAllocation(pResource);
            aOffsets[i] = pOffsets[i];
            aSizes[i] = pResource ? pResource->AllocationDesc.cbAllocation : 0;
        }
        else
        {
            aAllocations[i] = 0;
            aOffsets[i] = 0;
            aSizes[i] = 0;
        }
    }

    vboxDXSoSetTargets(pDevice, NumTargets, aAllocations, aOffsets, aSizes);
}

static void APIENTRY ddi10DrawAuto(D3D10DDI_HDEVICE hDevice)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p\n", pDevice));

    vboxDXDrawAuto(pDevice);
}

static void APIENTRY ddi10SetViewports(
    D3D10DDI_HDEVICE hDevice,
    UINT NumViewports,   // The total number of viewports that the pViewports parameter specifies.
    UINT ClearViewports, // The number of viewports after the number of viewports that NumViewports specifies to be set to NULL.
    const D3D10_DDI_VIEWPORT* pViewports // An array of D3D10_DDI_VIEWPORT structures for the viewports to set.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, NumViewports %u, ClearViewports %u\n", pDevice, NumViewports, ClearViewports));

    vboxDXSetViewports(pDevice, NumViewports, ClearViewports, pViewports);
}

static void APIENTRY ddi10SetScissorRects(
    D3D10DDI_HDEVICE hDevice,
    UINT NumRects,    // The total number of render-target portions that the pRects parameter specifies.
    UINT ClearRects,  // The number of render-target portions after the number of render-target portions that NumScissorRects specifies to be set to NULL.
    const D3D10_DDI_RECT* pRects // An array of scissor rectangles.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, NumRects %u, ClearRects %u\n", pDevice, NumRects, ClearRects));

    vboxDXSetScissorRects(pDevice, NumRects, ClearRects, pRects);
}

static void APIENTRY ddi10ClearRenderTargetView(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRENDERTARGETVIEW hRenderTargetView, // A handle to the render-target view to clear.
    FLOAT ColorRGBA[4] // A four-element array of single-precision float vectors that the driver uses to clear a render-target view.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXRENDERTARGETVIEW pRenderTargetView = (PVBOXDXRENDERTARGETVIEW)hRenderTargetView.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pRenderTargetView 0x%p", pDevice, pRenderTargetView));

    vboxDXClearRenderTargetView(pDevice, pRenderTargetView, ColorRGBA);
}

static void APIENTRY ddi10ClearDepthStencilView(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView, // A handle to the depth-stencil view to clear.
    UINT Flags,  // A value that specifies which parts of the buffer to affect - D3D10_DDI_CLEAR_DEPTH or D3D10_DDI_CLEAR_STENCIL.
    FLOAT Depth,  // A single-precision float vector to set the depth to.
    UINT8 Stencil  // An unsigned 8-bit integer value to set the stencil to.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXDEPTHSTENCILVIEW pDepthStencilView = (PVBOXDXDEPTHSTENCILVIEW)hDepthStencilView.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pDepthStencilView 0x%p", pDevice, pDepthStencilView));

    vboxDXClearDepthStencilView(pDevice, pDepthStencilView, Flags, Depth, Stencil);
}

static void APIENTRY ddi10SetPredication(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HQUERY hQuery, // A handle to the query object to set as a predicate.
    BOOL PredicateValue     // A Boolean value to compare with query data.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXQUERY pQuery = (PVBOXDXQUERY)hQuery.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pQuery 0x%p", pDevice, pQuery));

    vboxDXSetPredication(pDevice, pQuery, PredicateValue);
}

static void APIENTRY ddi10QueryGetData(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HQUERY hQuery, // A handle to the query object to poll.
    VOID* pData,   // A pointer to a region of memory that receives the data from a query operation.
    UINT DataSize, // The size, in bytes, of the query data that the pData parameter points to.
    UINT Flags     // Can be 0 or any combination of the flags enumerated by D3D11_ASYNC_GETDATA_FLAG.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXQUERY pQuery = (PVBOXDXQUERY)hQuery.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pQuery 0x%p", pDevice, pQuery));

    vboxDXQueryGetData(pDevice, pQuery, pData, DataSize, Flags);
}

static BOOL APIENTRY ddi11_1Flush(
    D3D10DDI_HDEVICE hDevice,
    UINT FlushFlags // A value from the D3D11_1_DDI_FLUSH_FLAGS enumeration that indicates whether
                    // the driver should continue to submit command buffers if there have been no new commands.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, Flags = 0x%x\n", pDevice, FlushFlags));

    HRESULT hr = vboxDXFlush(pDevice, !RT_BOOL(FlushFlags & D3D11_1DDI_FLUSH_UNLESS_NO_COMMANDS));
    return SUCCEEDED(hr);
}

static VOID APIENTRY ddi10Flush(
    D3D10DDI_HDEVICE hDevice
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p\n", pDevice));

    vboxDXFlush(pDevice, true);
}

static void APIENTRY ddi10GenMips(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView // A handle to the MIP-map texture surface.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADERRESOURCEVIEW pShaderResourceView = (PVBOXDXSHADERRESOURCEVIEW)hShaderResourceView.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShaderResourceView 0x%p", pDevice, pShaderResourceView));

    vboxDXGenMips(pDevice, pShaderResourceView);
}

static void APIENTRY ddi10ResourceCopy(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDstResource, // A handle to the destination resource to copy to.
    D3D10DDI_HRESOURCE hSrcResource  // A handle to the source resource to copy from.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pDstResource = (PVBOXDX_RESOURCE)hDstResource.pDrvPrivate;
    PVBOXDX_RESOURCE pSrcResource = (PVBOXDX_RESOURCE)hSrcResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pDstResource 0x%p, pSrcResource 0x%p",
                 pDevice, pDstResource, pSrcResource));

    vboxDXResourceCopy(pDevice, pDstResource, pSrcResource);
}

static void APIENTRY ddi10ResourceResolveSubresource(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDstResource, // A handle to the destination resource to resolve to. This resource must have been created as D3D10_USAGE_DEFAULT and single sampled.
    UINT DstSubresource,             // An index that indicates the destination subresource to resolve to.
    D3D10DDI_HRESOURCE hSrcResource, // A handle to the source resource to resolve from.
    UINT SrcSubresource,             // An index that indicates the source subresource to resolve from.
    DXGI_FORMAT ResolveFormat        // A DXGI_FORMAT-typed value that indicates how to interpret the contents of the resolved resource.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pDstResource = (PVBOXDX_RESOURCE)hDstResource.pDrvPrivate;
    PVBOXDX_RESOURCE pSrcResource = (PVBOXDX_RESOURCE)hSrcResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pDstResource 0x%p, pSrcResource 0x%p",
                 pDevice, pDstResource, pSrcResource));

    vboxDXResourceResolveSubresource(pDevice, pDstResource, DstSubresource,
                                     pSrcResource, SrcSubresource, ResolveFormat);
}

static void APIENTRY ddi10ResourceMap(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource, // A handle to the resource to map.
    UINT Subresource,             // An index that indicates the subresource to map.
    D3D10_DDI_MAP DDIMap,         // A D3D10_DDI_MAP-typed value that indicates the access level to map the subresource to.
    UINT Flags,                   // A D3D10_DDI_MAP_FLAG-typed value that indicates how to map the subresource.
    D3D10DDI_MAPPED_SUBRESOURCE* pMappedSubResource // A pointer to a D3D10DDI_MAPPED_SUBRESOURCE structure that receives the information about the mapped subresource.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)hResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pResource 0x%p, subres %d, map %d, flags 0x%X", pDevice, pResource, Subresource, DDIMap, Flags));

    vboxDXResourceMap(pDevice, pResource, Subresource, DDIMap, Flags, pMappedSubResource);
}

static void APIENTRY ddi10ResourceUnmap(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource, // A handle to the resource to unmap.
    UINT Subresource              // An index that indicates the subresource to unmap.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)hResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pResource 0x%p, subres %d", pDevice, pResource, Subresource));

    vboxDXResourceUnmap(pDevice, pResource, Subresource);
}

BOOL APIENTRY vboxDXResourceIsStagingBusy(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource
)
{
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, hResource);
    LogFlowFuncEnter();
    return TRUE;
}

static void APIENTRY ddi11_1RelocateDeviceFuncs(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_DEVICEFUNCS* pDeviceFunctions // The new location of the driver function table.
)
{
    /* This is usually a sign of trouble. Break into debugger. */
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, pDeviceFunctions);
    LogFlowFunc(("pDeviceFunctions %p", pDeviceFunctions));
    /* Nothing to do in this driver. */
}

static void APIENTRY ddi11RelocateDeviceFuncs(
    D3D10DDI_HDEVICE hDevice,
    D3D11DDI_DEVICEFUNCS* pDeviceFunctions // The new location of the driver function table.
)
{
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, pDeviceFunctions);
    LogFlowFunc(("pDeviceFunctions %p", pDeviceFunctions));
    /* Nothing to do in this driver. */
}

static void APIENTRY ddi10_1RelocateDeviceFuncs(
    D3D10DDI_HDEVICE hDevice,
    D3D10_1DDI_DEVICEFUNCS* pDeviceFunctions // The new location of the driver function table.
)
{
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, pDeviceFunctions);
    LogFlowFunc(("pDeviceFunctions %p", pDeviceFunctions));
    /* Nothing to do in this driver. */
}

static void APIENTRY ddi10RelocateDeviceFuncs(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_DEVICEFUNCS* pDeviceFunctions // The new location of the driver function table.
)
{
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, pDeviceFunctions);
    LogFlowFunc(("pDeviceFunctions %p", pDeviceFunctions));
    /* Nothing to do in this driver. */
}

static SIZE_T APIENTRY ddi11CalcPrivateResourceSize(
    D3D10DDI_HDEVICE hDevice,
    const D3D11DDIARG_CREATERESOURCE* pCreateResource // The parameters to calculate the size of the memory region.
)
{
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice);
    return RT_UOFFSETOF(VBOXDX_RESOURCE, aMipInfoList) + pCreateResource->MipLevels * sizeof(D3D10DDI_MIPINFO);
}

static SIZE_T APIENTRY ddi10CalcPrivateResourceSize(
    D3D10DDI_HDEVICE hDevice,
    const D3D10DDIARG_CREATERESOURCE* pCreateResource // The parameters to calculate the size of the memory region.
)
{
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice);
    return RT_UOFFSETOF(VBOXDX_RESOURCE, aMipInfoList) + pCreateResource->MipLevels * sizeof(D3D10DDI_MIPINFO);
}

static SIZE_T APIENTRY ddi10CalcPrivateOpenedResourceSize(
    D3D10DDI_HDEVICE hDevice,
    const D3D10DDIARG_OPENRESOURCE* pOpenResource // The parameters to calculate the size of the memory region.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;

    AssertReturnStmt(pOpenResource->NumAllocations == 1,
                     vboxDXDeviceSetError(pDevice, E_INVALIDARG), 0);
    AssertReturnStmt(pOpenResource->pOpenAllocationInfo2[0].PrivateDriverDataSize == sizeof(VBOXDXALLOCATIONDESC),
                     vboxDXDeviceSetError(pDevice, E_INVALIDARG), 0);

    VBOXDXALLOCATIONDESC const *pDesc = (VBOXDXALLOCATIONDESC *)pOpenResource->pOpenAllocationInfo2[0].pPrivateDriverData;
    return RT_UOFFSETOF(VBOXDX_RESOURCE, aMipInfoList) + pDesc->surfaceInfo.numMipLevels * sizeof(D3D10DDI_MIPINFO);
}

static const char* ResourceUsage2Str(D3D10_DDI_RESOURCE_USAGE usage)
{
    switch(usage)
    {
    case D3D10_DDI_USAGE_DEFAULT:
        return "DEFAULT";
    case D3D10_DDI_USAGE_IMMUTABLE:
        return "IMMUTABLE";
    case D3D10_DDI_USAGE_DYNAMIC:
        return "DYNAMIC";
    case D3D10_DDI_USAGE_STAGING:
        return "STAGING";
    default:
        return "UNKNOWN";
    }
}

static const char* ResourceMap2Str(D3D10_DDI_MAP  map)
{
    if (map == 0)
        return "";

    switch(map)
    {
    case D3D10_DDI_MAP_READ:
        return "R";
    case D3D10_DDI_MAP_WRITE:
        return "W";
    case D3D10_DDI_MAP_READWRITE:
        return "RW";
    case D3D10_DDI_MAP_WRITE_DISCARD:
        return "WD";
    case D3D10_DDI_MAP_WRITE_NOOVERWRITE:
        return "WN";
    default:
        return "UNKNOWN";
    }
}


static void APIENTRY ddi11CreateResource(
    D3D10DDI_HDEVICE hDevice,
    const D3D11DDIARG_CREATERESOURCE* pCreateResource, // The parameters that the user-mode display driver uses to create a resource.
    D3D10DDI_HRESOURCE hResource,    // A handle to the driver's private data for the resource.
    D3D10DDI_HRTRESOURCE hRTResource // A handle to the resource to use for calls back into the Direct3D runtime.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)hResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p pResource 0x%p, mipinfo (%d %d %d), pInitData 0x%p, resdim %d, usage %d %s, bind 0x%X, map 0x%X %s, misc 0x%X, format %d, miplevels %d, arraysize %d, stride %d",
        pDevice, pResource,
        pCreateResource->pMipInfoList[0].TexelWidth,
        pCreateResource->pMipInfoList[0].TexelHeight,
        pCreateResource->pMipInfoList[0].TexelDepth,
        pCreateResource->pInitialDataUP,
        pCreateResource->ResourceDimension,
        pCreateResource->Usage, ResourceUsage2Str((D3D10_DDI_RESOURCE_USAGE)pCreateResource->Usage),
        pCreateResource->BindFlags,
        pCreateResource->MapFlags, ResourceMap2Str((D3D10_DDI_MAP) pCreateResource->MapFlags),
        pCreateResource->MiscFlags,
        pCreateResource->Format,
        pCreateResource->MipLevels,
        pCreateResource->ArraySize,
        pCreateResource->ByteStride));

    pResource->hRTResource = hRTResource;
    int rc = vboxDXInitResourceData(pResource, pCreateResource);
    if (RT_SUCCESS(rc))
        vboxDXCreateResource(pDevice, pResource, pCreateResource);
}

static void APIENTRY ddi10CreateResource(
    D3D10DDI_HDEVICE hDevice,
    const D3D10DDIARG_CREATERESOURCE* pCreateResource, // The parameters that the user-mode display driver uses to create a resource.
    D3D10DDI_HRESOURCE hResource,    // A handle to the driver's private data for the resource.
    D3D10DDI_HRTRESOURCE hRTResource // A handle to the resource to use for calls back into the Direct3D runtime.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)hResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p pResource 0x%p, mipinfo (%d %d %d), pInitData 0x%p, resdim %d, usage %d %s, bind 0x%X, map 0x%X %s, misc 0x%X, format %d, miplevels %d, arraysize %d",
        pDevice, pResource,
        pCreateResource->pMipInfoList[0].TexelWidth,
        pCreateResource->pMipInfoList[0].TexelHeight,
        pCreateResource->pMipInfoList[0].TexelDepth,
        pCreateResource->pInitialDataUP,
        pCreateResource->ResourceDimension,
        pCreateResource->Usage, ResourceUsage2Str((D3D10_DDI_RESOURCE_USAGE)pCreateResource->Usage),
        pCreateResource->BindFlags,
        pCreateResource->MapFlags, ResourceMap2Str((D3D10_DDI_MAP) pCreateResource->MapFlags),
        pCreateResource->MiscFlags,
        pCreateResource->Format,
        pCreateResource->MipLevels,
        pCreateResource->ArraySize));

        D3D11DDIARG_CREATERESOURCE CreateResource;
        CreateResource.pMipInfoList      = pCreateResource->pMipInfoList;
        CreateResource.pInitialDataUP    = pCreateResource->pInitialDataUP;
        CreateResource.ResourceDimension = pCreateResource->ResourceDimension;
        CreateResource.Usage             = pCreateResource->Usage;
        CreateResource.BindFlags         = pCreateResource->BindFlags;
        CreateResource.MapFlags          = pCreateResource->MapFlags;
        CreateResource.MiscFlags         = pCreateResource->MiscFlags;
        CreateResource.Format            = pCreateResource->Format;
        CreateResource.SampleDesc        = pCreateResource->SampleDesc;
        CreateResource.MipLevels         = pCreateResource->MipLevels;
        CreateResource.ArraySize         = pCreateResource->ArraySize;
        CreateResource.pPrimaryDesc      = pCreateResource->pPrimaryDesc;
        CreateResource.ByteStride        = 0;
        CreateResource.DecoderBufferType = D3D11_1DDI_VIDEO_DECODER_BUFFER_UNKNOWN;
        CreateResource.TextureLayout     = D3DWDDM2_0DDI_TL_UNDEFINED;

    pResource->hRTResource = hRTResource;
    int rc = vboxDXInitResourceData(pResource, &CreateResource);
    if (RT_SUCCESS(rc))
        vboxDXCreateResource(pDevice, pResource, &CreateResource);
}

static void APIENTRY ddi10OpenResource(
    D3D10DDI_HDEVICE hDevice,
    const D3D10DDIARG_OPENRESOURCE* pOpenResource, //  The parameters that the user-mode display driver uses to open a shared resource.
    D3D10DDI_HRESOURCE hResource,    // A handle to the driver's private data for the resource.
    D3D10DDI_HRTRESOURCE hRTResource // A handle to the resource to use for calls back into the Direct3D runtime.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)hResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p pResource 0x%p, NumAllocations %d", pDevice, pResource, pOpenResource->NumAllocations));

    pResource->hRTResource = hRTResource;
    vboxDXOpenResource(pDevice, pResource, pOpenResource);
}

static void APIENTRY ddi10DestroyResource(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource // A handle to the driver's private data for the resource.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)hResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p pResource 0x%p", pDevice, pResource));

    vboxDXDestroyResource(pDevice, pResource);
}

static SIZE_T APIENTRY ddi11CalcPrivateShaderResourceViewSize(
    D3D10DDI_HDEVICE hDevice,
    const D3D11DDIARG_CREATESHADERRESOURCEVIEW* pCreateShaderResourceView // Describes the shader resource view to create.
)
{
    RT_NOREF(hDevice, pCreateShaderResourceView);
    return sizeof(VBOXDXSHADERRESOURCEVIEW);
}

static SIZE_T APIENTRY ddi10_1CalcPrivateShaderResourceViewSize(
    D3D10DDI_HDEVICE hDevice,
    const D3D10_1DDIARG_CREATESHADERRESOURCEVIEW* pCreateShaderResourceView // Describes the shader resource view to create.
)
{
    RT_NOREF(hDevice, pCreateShaderResourceView);
    return sizeof(VBOXDXSHADERRESOURCEVIEW);
}

static SIZE_T APIENTRY ddi10CalcPrivateShaderResourceViewSize(
    D3D10DDI_HDEVICE hDevice,
    const D3D10DDIARG_CREATESHADERRESOURCEVIEW* pCreateShaderResourceView // Describes the shader resource view to create.
)
{
    RT_NOREF(hDevice, pCreateShaderResourceView);
    return sizeof(VBOXDXSHADERRESOURCEVIEW);
}

static void APIENTRY ddi11CreateShaderResourceView(
    D3D10DDI_HDEVICE hDevice,
    const D3D11DDIARG_CREATESHADERRESOURCEVIEW* pCreateShaderResourceView,
    D3D10DDI_HSHADERRESOURCEVIEW   hShaderResourceView,  // A handle to the driver's private data for the shader.
    D3D10DDI_HRTSHADERRESOURCEVIEW hRTShaderResourceView // A handle to the shader to use for calls back into the Direct3D runtime.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)pCreateShaderResourceView->hDrvResource.pDrvPrivate;
    PVBOXDXSHADERRESOURCEVIEW pShaderResourceView = (PVBOXDXSHADERRESOURCEVIEW)hShaderResourceView.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pResource %p, pShaderResourceView 0x%p", pDevice, pResource, pShaderResourceView));

    pShaderResourceView->hRTShaderResourceView = hRTShaderResourceView;
    pShaderResourceView->pResource = pResource;
    pShaderResourceView->Format = pCreateShaderResourceView->Format;
    pShaderResourceView->ResourceDimension = pCreateShaderResourceView->ResourceDimension;
    switch (pShaderResourceView->ResourceDimension)
    {
        case D3D10DDIRESOURCE_BUFFER:
            pShaderResourceView->DimensionDesc.Buffer = pCreateShaderResourceView->Buffer;
            break;
        case D3D10DDIRESOURCE_TEXTURE1D:
            pShaderResourceView->DimensionDesc.Tex1D = pCreateShaderResourceView->Tex1D;
            break;
        case D3D10DDIRESOURCE_TEXTURE2D:
            pShaderResourceView->DimensionDesc.Tex2D = pCreateShaderResourceView->Tex2D;
            break;
        case D3D10DDIRESOURCE_TEXTURE3D:
            pShaderResourceView->DimensionDesc.Tex3D = pCreateShaderResourceView->Tex3D;
            break;
        case D3D10DDIRESOURCE_TEXTURECUBE:
            pShaderResourceView->DimensionDesc.TexCube = pCreateShaderResourceView->TexCube;
            break;
        case D3D11DDIRESOURCE_BUFFEREX:
            pShaderResourceView->DimensionDesc.BufferEx = pCreateShaderResourceView->BufferEx;
            break;
        default:
            vboxDXDeviceSetError(pDevice, E_INVALIDARG);
            return;
    }

    vboxDXCreateShaderResourceView(pDevice, pShaderResourceView);
}

static void APIENTRY ddi10_1CreateShaderResourceView(
    D3D10DDI_HDEVICE hDevice,
    const D3D10_1DDIARG_CREATESHADERRESOURCEVIEW* pCreateShaderResourceView,
    D3D10DDI_HSHADERRESOURCEVIEW   hShaderResourceView,  // A handle to the driver's private data for the shader.
    D3D10DDI_HRTSHADERRESOURCEVIEW hRTShaderResourceView // A handle to the shader to use for calls back into the Direct3D runtime.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)pCreateShaderResourceView->hDrvResource.pDrvPrivate;
    PVBOXDXSHADERRESOURCEVIEW pShaderResourceView = (PVBOXDXSHADERRESOURCEVIEW)hShaderResourceView.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pResource %p, pShaderResourceView 0x%p", pDevice, pResource, pShaderResourceView));

    pShaderResourceView->hRTShaderResourceView = hRTShaderResourceView;
    pShaderResourceView->pResource = pResource;
    pShaderResourceView->Format = pCreateShaderResourceView->Format;
    pShaderResourceView->ResourceDimension = pCreateShaderResourceView->ResourceDimension;
    switch (pShaderResourceView->ResourceDimension)
    {
        case D3D10DDIRESOURCE_BUFFER:
            pShaderResourceView->DimensionDesc.Buffer = pCreateShaderResourceView->Buffer;
            break;
        case D3D10DDIRESOURCE_TEXTURE1D:
            pShaderResourceView->DimensionDesc.Tex1D = pCreateShaderResourceView->Tex1D;
            break;
        case D3D10DDIRESOURCE_TEXTURE2D:
            pShaderResourceView->DimensionDesc.Tex2D = pCreateShaderResourceView->Tex2D;
            break;
        case D3D10DDIRESOURCE_TEXTURE3D:
            pShaderResourceView->DimensionDesc.Tex3D = pCreateShaderResourceView->Tex3D;
            break;
        case D3D10DDIRESOURCE_TEXTURECUBE:
            pShaderResourceView->DimensionDesc.TexCube = pCreateShaderResourceView->TexCube;
            break;
        default:
            vboxDXDeviceSetError(pDevice, E_INVALIDARG);
            return;
    }

    vboxDXCreateShaderResourceView(pDevice, pShaderResourceView);
}

static void APIENTRY ddi10CreateShaderResourceView(
    D3D10DDI_HDEVICE hDevice,
    const D3D10DDIARG_CREATESHADERRESOURCEVIEW* pCreateShaderResourceView,
    D3D10DDI_HSHADERRESOURCEVIEW   hShaderResourceView,  // A handle to the driver's private data for the shader.
    D3D10DDI_HRTSHADERRESOURCEVIEW hRTShaderResourceView // A handle to the shader to use for calls back into the Direct3D runtime.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)pCreateShaderResourceView->hDrvResource.pDrvPrivate;
    PVBOXDXSHADERRESOURCEVIEW pShaderResourceView = (PVBOXDXSHADERRESOURCEVIEW)hShaderResourceView.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pResource %p, pShaderResourceView 0x%p", pDevice, pResource, pShaderResourceView));

    pShaderResourceView->hRTShaderResourceView = hRTShaderResourceView;
    pShaderResourceView->pResource = pResource;
    pShaderResourceView->Format = pCreateShaderResourceView->Format;
    pShaderResourceView->ResourceDimension = pCreateShaderResourceView->ResourceDimension;
    switch (pShaderResourceView->ResourceDimension)
    {
        case D3D10DDIRESOURCE_BUFFER:
            pShaderResourceView->DimensionDesc.Buffer = pCreateShaderResourceView->Buffer;
            break;
        case D3D10DDIRESOURCE_TEXTURE1D:
            pShaderResourceView->DimensionDesc.Tex1D = pCreateShaderResourceView->Tex1D;
            break;
        case D3D10DDIRESOURCE_TEXTURE2D:
            pShaderResourceView->DimensionDesc.Tex2D = pCreateShaderResourceView->Tex2D;
            break;
        case D3D10DDIRESOURCE_TEXTURE3D:
            pShaderResourceView->DimensionDesc.Tex3D = pCreateShaderResourceView->Tex3D;
            break;
        case D3D10DDIRESOURCE_TEXTURECUBE:
            pShaderResourceView->DimensionDesc.TexCube.MostDetailedMip  = pCreateShaderResourceView->TexCube.MostDetailedMip;
            pShaderResourceView->DimensionDesc.TexCube.MipLevels        = pCreateShaderResourceView->TexCube.MipLevels;
            pShaderResourceView->DimensionDesc.TexCube.First2DArrayFace = 0;
            pShaderResourceView->DimensionDesc.TexCube.NumCubes         = 1;
            break;
        default:
            vboxDXDeviceSetError(pDevice, E_INVALIDARG);
            return;
    }

    vboxDXCreateShaderResourceView(pDevice, pShaderResourceView);
}

static void APIENTRY ddi10DestroyShaderResourceView(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView // A handle to the driver's private data for the shader resource view object to destroy.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADERRESOURCEVIEW pShaderResourceView = (PVBOXDXSHADERRESOURCEVIEW)hShaderResourceView.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShaderResourceView 0x%p", pDevice, pShaderResourceView));

    vboxDXDestroyShaderResourceView(pDevice, pShaderResourceView);
}

static SIZE_T APIENTRY ddi10CalcPrivateRenderTargetViewSize(
    D3D10DDI_HDEVICE hDevice,
    const D3D10DDIARG_CREATERENDERTARGETVIEW* pCreateRenderTargetView // Describes the render target view to create.
)
{
    RT_NOREF(hDevice, pCreateRenderTargetView);
    return sizeof(VBOXDXRENDERTARGETVIEW);
}

static void APIENTRY ddi10CreateRenderTargetView(
    D3D10DDI_HDEVICE hDevice,
    const D3D10DDIARG_CREATERENDERTARGETVIEW* pCreateRenderTargetView,
    D3D10DDI_HRENDERTARGETVIEW hRenderTargetView,
    D3D10DDI_HRTRENDERTARGETVIEW hRTRenderTargetView
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)pCreateRenderTargetView->hDrvResource.pDrvPrivate;
    PVBOXDXRENDERTARGETVIEW pRenderTargetView = (PVBOXDXRENDERTARGETVIEW)hRenderTargetView.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pResource %p, pRenderTargetView 0x%p", pDevice, pResource, pRenderTargetView));

    pRenderTargetView->hRTRenderTargetView = hRTRenderTargetView;
    pRenderTargetView->pResource = pResource;
    pRenderTargetView->Format = pCreateRenderTargetView->Format;
    pRenderTargetView->ResourceDimension = pCreateRenderTargetView->ResourceDimension;
    switch (pRenderTargetView->ResourceDimension)
    {
        case D3D10DDIRESOURCE_BUFFER:
            pRenderTargetView->DimensionDesc.Buffer = pCreateRenderTargetView->Buffer;
            break;
        case D3D10DDIRESOURCE_TEXTURE1D:
            pRenderTargetView->DimensionDesc.Tex1D = pCreateRenderTargetView->Tex1D;
            break;
        case D3D10DDIRESOURCE_TEXTURE2D:
            pRenderTargetView->DimensionDesc.Tex2D = pCreateRenderTargetView->Tex2D;
            break;
        case D3D10DDIRESOURCE_TEXTURE3D:
            pRenderTargetView->DimensionDesc.Tex3D = pCreateRenderTargetView->Tex3D;
            break;
        case D3D10DDIRESOURCE_TEXTURECUBE:
            pRenderTargetView->DimensionDesc.TexCube = pCreateRenderTargetView->TexCube;
            break;
        default:
            vboxDXDeviceSetError(pDevice, E_INVALIDARG);
            return;
    }

    vboxDXCreateRenderTargetView(pDevice, pRenderTargetView);
}

static void APIENTRY ddi10DestroyRenderTargetView(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRENDERTARGETVIEW hRenderTargetView
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXRENDERTARGETVIEW pRenderTargetView = (PVBOXDXRENDERTARGETVIEW)hRenderTargetView.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pRenderTargetView 0x%p", pDevice, pRenderTargetView));

    vboxDXDestroyRenderTargetView(pDevice, pRenderTargetView);
}

static SIZE_T APIENTRY ddi11CalcPrivateDepthStencilViewSize(
    D3D10DDI_HDEVICE hDevice,
    const D3D11DDIARG_CREATEDEPTHSTENCILVIEW* pCreateDepthStencilView
)
{
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, pCreateDepthStencilView);
    return sizeof(VBOXDXDEPTHSTENCILVIEW);
}

static SIZE_T APIENTRY ddi10CalcPrivateDepthStencilViewSize(
    D3D10DDI_HDEVICE hDevice,
    const D3D10DDIARG_CREATEDEPTHSTENCILVIEW* pCreateDepthStencilView
)
{
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, pCreateDepthStencilView);
    return sizeof(VBOXDXDEPTHSTENCILVIEW);
}

static void APIENTRY ddi11CreateDepthStencilView(
    D3D10DDI_HDEVICE hDevice,
    const D3D11DDIARG_CREATEDEPTHSTENCILVIEW* pCreateDepthStencilView,
    D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView,
    D3D10DDI_HRTDEPTHSTENCILVIEW hRTDepthStencilView
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)pCreateDepthStencilView->hDrvResource.pDrvPrivate;
    PVBOXDXDEPTHSTENCILVIEW pDepthStencilView = (PVBOXDXDEPTHSTENCILVIEW)hDepthStencilView.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pResource %p, pDepthStencilView 0x%p", pDevice, pResource, pDepthStencilView));

    pDepthStencilView->hRTDepthStencilView = hRTDepthStencilView;
    pDepthStencilView->pResource = pResource;
    pDepthStencilView->Format = pCreateDepthStencilView->Format;
    pDepthStencilView->ResourceDimension = pCreateDepthStencilView->ResourceDimension;
    pDepthStencilView->Flags = pCreateDepthStencilView->Flags;
    switch (pDepthStencilView->ResourceDimension)
    {
        case D3D10DDIRESOURCE_TEXTURE1D:
            pDepthStencilView->DimensionDesc.Tex1D = pCreateDepthStencilView->Tex1D;
            break;
        case D3D10DDIRESOURCE_TEXTURE2D:
            pDepthStencilView->DimensionDesc.Tex2D = pCreateDepthStencilView->Tex2D;
            break;
        case D3D10DDIRESOURCE_TEXTURECUBE:
            pDepthStencilView->DimensionDesc.TexCube = pCreateDepthStencilView->TexCube;
            break;
        default:
            vboxDXDeviceSetError(pDevice, E_INVALIDARG);
            return;
    }

    vboxDXCreateDepthStencilView(pDevice, pDepthStencilView);
}

static void APIENTRY ddi10CreateDepthStencilView(
    D3D10DDI_HDEVICE hDevice,
    const D3D10DDIARG_CREATEDEPTHSTENCILVIEW* pCreateDepthStencilView,
    D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView,
    D3D10DDI_HRTDEPTHSTENCILVIEW hRTDepthStencilView
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)pCreateDepthStencilView->hDrvResource.pDrvPrivate;
    PVBOXDXDEPTHSTENCILVIEW pDepthStencilView = (PVBOXDXDEPTHSTENCILVIEW)hDepthStencilView.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pResource %p, pDepthStencilView 0x%p", pDevice, pResource, pDepthStencilView));

    pDepthStencilView->hRTDepthStencilView = hRTDepthStencilView;
    pDepthStencilView->pResource = pResource;
    pDepthStencilView->Format = pCreateDepthStencilView->Format;
    pDepthStencilView->ResourceDimension = pCreateDepthStencilView->ResourceDimension;
    switch (pDepthStencilView->ResourceDimension)
    {
        case D3D10DDIRESOURCE_TEXTURE1D:
            pDepthStencilView->DimensionDesc.Tex1D = pCreateDepthStencilView->Tex1D;
            break;
        case D3D10DDIRESOURCE_TEXTURE2D:
            pDepthStencilView->DimensionDesc.Tex2D = pCreateDepthStencilView->Tex2D;
            break;
        case D3D10DDIRESOURCE_TEXTURECUBE:
            pDepthStencilView->DimensionDesc.TexCube = pCreateDepthStencilView->TexCube;
            break;
        default:
            vboxDXDeviceSetError(pDevice, E_INVALIDARG);
            return;
    }

    vboxDXCreateDepthStencilView(pDevice, pDepthStencilView);
}

static void APIENTRY ddi10DestroyDepthStencilView(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXDEPTHSTENCILVIEW pDepthStencilView = (PVBOXDXDEPTHSTENCILVIEW)hDepthStencilView.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pDepthStencilView 0x%p", pDevice, pDepthStencilView));

    vboxDXDestroyDepthStencilView(pDevice, pDepthStencilView);
}

static SIZE_T APIENTRY ddi10CalcPrivateElementLayoutSize(
    D3D10DDI_HDEVICE hDevice,
    const D3D10DDIARG_CREATEELEMENTLAYOUT* pCreateElementLayout
)
{
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice);
    return RT_UOFFSETOF(VBOXDXELEMENTLAYOUT, aVertexElements)
           + pCreateElementLayout->NumElements * sizeof(D3D10DDIARG_INPUT_ELEMENT_DESC);
}

static void APIENTRY ddi10CreateElementLayout(
    D3D10DDI_HDEVICE hDevice,
    const D3D10DDIARG_CREATEELEMENTLAYOUT* pCreateElementLayout,
    D3D10DDI_HELEMENTLAYOUT hElementLayout,
    D3D10DDI_HRTELEMENTLAYOUT hRTElementLayout
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXELEMENTLAYOUT pElementLayout = (PVBOXDXELEMENTLAYOUT)hElementLayout.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pElementLayout 0x%p", pDevice, pElementLayout));

    pElementLayout->hRTElementLayout = hRTElementLayout;
    pElementLayout->NumElements = pCreateElementLayout->NumElements;
    for (unsigned i = 0; i < pCreateElementLayout->NumElements; ++i)
        pElementLayout->aVertexElements[i] = pCreateElementLayout->pVertexElements[i];

    vboxDXCreateElementLayout(pDevice, pElementLayout);
}

static void APIENTRY ddi10DestroyElementLayout(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HELEMENTLAYOUT hElementLayout
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXELEMENTLAYOUT pElementLayout = (PVBOXDXELEMENTLAYOUT)hElementLayout.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pElementLayout 0x%p", pDevice, pElementLayout));

    vboxDXDestroyElementLayout(pDevice, pElementLayout);
}

static SIZE_T APIENTRY ddi11_1CalcPrivateBlendStateSize(
    D3D10DDI_HDEVICE hDevice,
    const D3D11_1_DDI_BLEND_DESC* pBlendDesc
)
{
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, pBlendDesc);
    return sizeof(VBOXDX_BLENDSTATE);
}

static SIZE_T APIENTRY ddi10_1CalcPrivateBlendStateSize(
    D3D10DDI_HDEVICE hDevice,
    const D3D10_1_DDI_BLEND_DESC* pBlendDesc
)
{
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, pBlendDesc);
    return sizeof(VBOXDX_BLENDSTATE);
}

static SIZE_T APIENTRY ddi10CalcPrivateBlendStateSize(
    D3D10DDI_HDEVICE hDevice,
    const D3D10_DDI_BLEND_DESC* pBlendDesc
)
{
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, pBlendDesc);
    return sizeof(VBOXDX_BLENDSTATE);
}

static void APIENTRY ddi11_1CreateBlendState(
    D3D10DDI_HDEVICE hDevice,
    const D3D11_1_DDI_BLEND_DESC* pBlendDesc,
    D3D10DDI_HBLENDSTATE hBlendState,
    D3D10DDI_HRTBLENDSTATE hRTBlendState
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_BLENDSTATE pBlendState = (PVBOXDX_BLENDSTATE)hBlendState.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pBlendState 0x%p, RT[0] BlendEnable %d\n", pDevice, pBlendState, pBlendDesc->RenderTarget[0].BlendEnable));

    /* Init the blend state and allocate blend id. */
    pBlendState->hRTBlendState = hRTBlendState;
    pBlendState->BlendDesc = *pBlendDesc;

    vboxDXCreateBlendState(pDevice, pBlendState);
}

static void APIENTRY ddi10_1CreateBlendState(
    D3D10DDI_HDEVICE hDevice,
    const D3D10_1_DDI_BLEND_DESC* pBlendDesc,
    D3D10DDI_HBLENDSTATE hBlendState,
    D3D10DDI_HRTBLENDSTATE hRTBlendState
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_BLENDSTATE pBlendState = (PVBOXDX_BLENDSTATE)hBlendState.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pBlendState 0x%p, RT[0] BlendEnable %d\n", pDevice, pBlendState, pBlendDesc->RenderTarget[0].BlendEnable));

    /* Init the blend state and allocate blend id. */
    pBlendState->hRTBlendState = hRTBlendState;
    pBlendState->BlendDesc.AlphaToCoverageEnable = pBlendDesc->AlphaToCoverageEnable;
    pBlendState->BlendDesc.IndependentBlendEnable = pBlendDesc->IndependentBlendEnable;
    for (unsigned i = 0; i < D3D10_DDI_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        D3D10_DDI_RENDER_TARGET_BLEND_DESC1 const *src = &pBlendDesc->RenderTarget[i];
        D3D11_1_DDI_RENDER_TARGET_BLEND_DESC *dst = &pBlendState->BlendDesc.RenderTarget[i];
        dst->BlendEnable           = src->BlendEnable;
        dst->LogicOpEnable         = FALSE;
        dst->SrcBlend              = src->SrcBlend;
        dst->DestBlend             = src->DestBlend;
        dst->BlendOp               = src->BlendOp;
        dst->SrcBlendAlpha         = src->SrcBlendAlpha;
        dst->DestBlendAlpha        = src->DestBlendAlpha;
        dst->BlendOpAlpha          = src->BlendOpAlpha;
        dst->LogicOp               = D3D11_1_DDI_LOGIC_OP_CLEAR;
        dst->RenderTargetWriteMask = src->RenderTargetWriteMask;
    }

    vboxDXCreateBlendState(pDevice, pBlendState);
}

static void APIENTRY ddi10CreateBlendState(
    D3D10DDI_HDEVICE hDevice,
    const D3D10_DDI_BLEND_DESC* pBlendDesc,
    D3D10DDI_HBLENDSTATE hBlendState,
    D3D10DDI_HRTBLENDSTATE hRTBlendState
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_BLENDSTATE pBlendState = (PVBOXDX_BLENDSTATE)hBlendState.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pBlendState 0x%p, RT[0] BlendEnable %d", pDevice, pBlendState, pBlendDesc->BlendEnable[0]));

    /* Init the blend state and allocate blend id. */
    pBlendState->hRTBlendState = hRTBlendState;
    pBlendState->BlendDesc.AlphaToCoverageEnable = pBlendDesc->AlphaToCoverageEnable;
    for (unsigned i = 0; i < D3D10_DDI_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        D3D11_1_DDI_RENDER_TARGET_BLEND_DESC *dst = &pBlendState->BlendDesc.RenderTarget[i];
        dst->BlendEnable           = pBlendDesc->BlendEnable[i];
        dst->LogicOpEnable         = FALSE;
        dst->SrcBlend              = pBlendDesc->SrcBlend;
        dst->DestBlend             = pBlendDesc->DestBlend;
        dst->BlendOp               = pBlendDesc->BlendOp;
        dst->SrcBlendAlpha         = pBlendDesc->SrcBlendAlpha;
        dst->DestBlendAlpha        = pBlendDesc->DestBlendAlpha;
        dst->BlendOpAlpha          = pBlendDesc->BlendOpAlpha;
        dst->LogicOp               = D3D11_1_DDI_LOGIC_OP_CLEAR;
        dst->RenderTargetWriteMask = pBlendDesc->RenderTargetWriteMask[i];
    }

    vboxDXCreateBlendState(pDevice, pBlendState);
}

static void APIENTRY ddi10DestroyBlendState(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HBLENDSTATE hBlendState
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_BLENDSTATE pBlendState = (PVBOXDX_BLENDSTATE)hBlendState.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, hBlendState 0x%p", pDevice, pBlendState));

    vboxDXDestroyBlendState(pDevice, pBlendState);
}

static SIZE_T APIENTRY ddi10CalcPrivateDepthStencilStateSize(
    D3D10DDI_HDEVICE hDevice,
    const D3D10_DDI_DEPTH_STENCIL_DESC* pDepthStencilDesc
)
{
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, pDepthStencilDesc);
    return sizeof(VBOXDX_DEPTHSTENCIL_STATE);
}

static void APIENTRY ddi10CreateDepthStencilState(
    D3D10DDI_HDEVICE hDevice,
    const D3D10_DDI_DEPTH_STENCIL_DESC* pDepthStencilDesc,
    D3D10DDI_HDEPTHSTENCILSTATE hDepthStencilState,
    D3D10DDI_HRTDEPTHSTENCILSTATE hRTDepthStencilState
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_DEPTHSTENCIL_STATE pDepthStencilState = (PVBOXDX_DEPTHSTENCIL_STATE)hDepthStencilState.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, hDepthStencilState 0x%p, DepthEnable %d, StencilEnable %d", pDevice, hDepthStencilState, pDepthStencilDesc->DepthEnable, pDepthStencilDesc->StencilEnable));

    pDepthStencilState->hRTDepthStencilState = hRTDepthStencilState;
    pDepthStencilState->DepthStencilDesc = *pDepthStencilDesc;

    vboxDXCreateDepthStencilState(pDevice, pDepthStencilState);
}

static void APIENTRY ddi10DestroyDepthStencilState(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HDEPTHSTENCILSTATE hDepthStencilState
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_DEPTHSTENCIL_STATE pDepthStencilState = (PVBOXDX_DEPTHSTENCIL_STATE)hDepthStencilState.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pDepthStencilState 0x%p", pDevice, pDepthStencilState));

    vboxDXDestroyDepthStencilState(pDevice, pDepthStencilState);
}

static SIZE_T APIENTRY ddi11_1CalcPrivateRasterizerStateSize(
    D3D10DDI_HDEVICE hDevice,
    const D3D11_1_DDI_RASTERIZER_DESC* pRasterizerDesc
)
{
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, pRasterizerDesc);
    return sizeof(VBOXDX_RASTERIZER_STATE);
}

static SIZE_T APIENTRY ddi10CalcPrivateRasterizerStateSize(
    D3D10DDI_HDEVICE hDevice,
    const D3D10_DDI_RASTERIZER_DESC* pRasterizerDesc
)
{
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, pRasterizerDesc);
    return sizeof(VBOXDX_RASTERIZER_STATE);
}

static void APIENTRY ddi11_1CreateRasterizerState(
    D3D10DDI_HDEVICE hDevice,
    const D3D11_1_DDI_RASTERIZER_DESC* pRasterizerDesc,
    D3D10DDI_HRASTERIZERSTATE hRasterizerState,
    D3D10DDI_HRTRASTERIZERSTATE hRTRasterizerState
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RASTERIZER_STATE pRasterizerState = (PVBOXDX_RASTERIZER_STATE)hRasterizerState.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, hRasterizerState 0x%p, FillMode %d, CullMode %d", pDevice, hRasterizerState, pRasterizerDesc->FillMode, pRasterizerDesc->CullMode));

    pRasterizerState->hRTRasterizerState = hRTRasterizerState;
    pRasterizerState->RasterizerDesc = *pRasterizerDesc;

    vboxDXCreateRasterizerState(pDevice, pRasterizerState);
}

static void APIENTRY ddi10CreateRasterizerState(
    D3D10DDI_HDEVICE hDevice,
    const D3D10_DDI_RASTERIZER_DESC* pRasterizerDesc,
    D3D10DDI_HRASTERIZERSTATE hRasterizerState,
    D3D10DDI_HRTRASTERIZERSTATE hRTRasterizerState
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RASTERIZER_STATE pRasterizerState = (PVBOXDX_RASTERIZER_STATE)hRasterizerState.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, hRasterizerState 0x%p, FillMode %d, CullMode %d", pDevice, hRasterizerState, pRasterizerDesc->FillMode, pRasterizerDesc->CullMode));

    pRasterizerState->hRTRasterizerState = hRTRasterizerState;
    pRasterizerState->RasterizerDesc.FillMode              = pRasterizerDesc->FillMode;
    pRasterizerState->RasterizerDesc.CullMode              = pRasterizerDesc->CullMode;
    pRasterizerState->RasterizerDesc.FrontCounterClockwise = pRasterizerDesc->FrontCounterClockwise;
    pRasterizerState->RasterizerDesc.DepthBias             = pRasterizerDesc->DepthBias;
    pRasterizerState->RasterizerDesc.DepthBiasClamp        = pRasterizerDesc->DepthBiasClamp;
    pRasterizerState->RasterizerDesc.SlopeScaledDepthBias  = pRasterizerDesc->SlopeScaledDepthBias;
    pRasterizerState->RasterizerDesc.DepthClipEnable       = pRasterizerDesc->DepthClipEnable;
    pRasterizerState->RasterizerDesc.ScissorEnable         = pRasterizerDesc->ScissorEnable;
    pRasterizerState->RasterizerDesc.MultisampleEnable     = pRasterizerDesc->MultisampleEnable;
    pRasterizerState->RasterizerDesc.AntialiasedLineEnable = pRasterizerDesc->AntialiasedLineEnable;
    pRasterizerState->RasterizerDesc.ForcedSampleCount     = 0;

    vboxDXCreateRasterizerState(pDevice, pRasterizerState);
}

static void APIENTRY ddi10DestroyRasterizerState(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRASTERIZERSTATE hRasterizerState
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RASTERIZER_STATE pRasterizerState = (PVBOXDX_RASTERIZER_STATE)hRasterizerState.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, hRasterizerState 0x%p", pDevice, pRasterizerState));

    vboxDXDestroyRasterizerState(pDevice, pRasterizerState);
}

static SIZE_T APIENTRY ddi11_1CalcPrivateShaderSize(
    D3D10DDI_HDEVICE hDevice,
    const UINT* pShaderCode, // A pointer to an array of CONST UINT tokens that make up the shader code.
    const D3D11_1DDIARG_STAGE_IO_SIGNATURES* pSignatures
)
{
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice);
    return sizeof(VBOXDXSHADER)
         + pShaderCode[1] * sizeof(UINT)
         + sizeof(SVGA3dDXSignatureHeader)
         + pSignatures->NumInputSignatureEntries * sizeof(SVGA3dDXShaderSignatureEntry)
         + pSignatures->NumOutputSignatureEntries * sizeof(SVGA3dDXShaderSignatureEntry);
}

static SIZE_T APIENTRY ddi10CalcPrivateShaderSize(
    D3D10DDI_HDEVICE hDevice,
    const UINT* pShaderCode, // A pointer to an array of CONST UINT tokens that make up the shader code.
    const D3D10DDIARG_STAGE_IO_SIGNATURES* pSignatures
)
{
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice);
    return sizeof(VBOXDXSHADER)
         + pShaderCode[1] * sizeof(UINT)
         + sizeof(SVGA3dDXSignatureHeader)
         + pSignatures->NumInputSignatureEntries * sizeof(SVGA3dDXShaderSignatureEntry)
         + pSignatures->NumOutputSignatureEntries * sizeof(SVGA3dDXShaderSignatureEntry);
}

static void APIENTRY ddi11_1CreateVertexShader(
    D3D10DDI_HDEVICE hDevice,
    const UINT* pShaderCode,
    D3D10DDI_HSHADER hShader,
    D3D10DDI_HRTSHADER hRTShader,
    const D3D11_1DDIARG_STAGE_IO_SIGNATURES* pSignatures
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADER pShader = (PVBOXDXSHADER)hShader.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShader 0x%p, ShaderCode: %X %X ...", pDevice, pShader, pShaderCode[0], pShaderCode[1]));

    pShader->hRTShader = hRTShader;

    vboxDXCreateShader(pDevice, SVGA3D_SHADERTYPE_VS, pShader, pShaderCode,
                       pSignatures->pInputSignature, pSignatures->NumInputSignatureEntries,
                       pSignatures->pOutputSignature, pSignatures->NumOutputSignatureEntries, NULL, 0);
}

static void APIENTRY ddi10CreateVertexShader(
    D3D10DDI_HDEVICE hDevice,
    const UINT* pShaderCode,
    D3D10DDI_HSHADER hShader,
    D3D10DDI_HRTSHADER hRTShader,
    const D3D10DDIARG_STAGE_IO_SIGNATURES* pSignatures
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADER pShader = (PVBOXDXSHADER)hShader.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShader 0x%p, ShaderCode: %X %X ...", pDevice, pShader, pShaderCode[0], pShaderCode[1]));

    pShader->hRTShader = hRTShader;

    D3D11_1DDIARG_SIGNATURE_ENTRY2 *paInputSignature = NULL;
    D3D11_1DDIARG_SIGNATURE_ENTRY2 *paOutputSignature = NULL;
    D3D11_1DDIARG_SIGNATURE_ENTRY2 *paSignature = NULL;
    uint32_t const cSignatures = pSignatures->NumInputSignatureEntries + pSignatures->NumOutputSignatureEntries;
    if (cSignatures)
    {
        paSignature = (D3D11_1DDIARG_SIGNATURE_ENTRY2 *)RTMemTmpAlloc(cSignatures * (sizeof(D3D11_1DDIARG_SIGNATURE_ENTRY2)));
        AssertReturnVoidStmt(paSignature, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));
        paInputSignature = paSignature;
        paOutputSignature = &paSignature[pSignatures->NumInputSignatureEntries];

        for (unsigned i = 0; i < pSignatures->NumInputSignatureEntries; ++i)
        {
            paInputSignature[i].SystemValue           = pSignatures->pInputSignature[i].SystemValue;
            paInputSignature[i].Register              = pSignatures->pInputSignature[i].Register;
            paInputSignature[i].Mask                  = pSignatures->pInputSignature[i].Mask;
            paInputSignature[i].RegisterComponentType = D3D10_SB_REGISTER_COMPONENT_UNKNOWN;
            paInputSignature[i].MinPrecision          = D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT;
        }

        for (unsigned i = 0; i < pSignatures->NumOutputSignatureEntries; ++i)
        {
            paOutputSignature[i].SystemValue           = pSignatures->pOutputSignature[i].SystemValue;
            paOutputSignature[i].Register              = pSignatures->pOutputSignature[i].Register;
            paOutputSignature[i].Mask                  = pSignatures->pOutputSignature[i].Mask;
            paOutputSignature[i].RegisterComponentType = D3D10_SB_REGISTER_COMPONENT_UNKNOWN;
            paOutputSignature[i].MinPrecision          = D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT;
        }
    }

    vboxDXCreateShader(pDevice, SVGA3D_SHADERTYPE_VS, pShader, pShaderCode,
                       paInputSignature, pSignatures->NumInputSignatureEntries,
                       paOutputSignature, pSignatures->NumOutputSignatureEntries, NULL, 0);
    RTMemTmpFree(paSignature);
}

static void APIENTRY ddi11_1CreateGeometryShader(
    D3D10DDI_HDEVICE hDevice,
    const UINT* pShaderCode,
    D3D10DDI_HSHADER hShader,
    D3D10DDI_HRTSHADER hRTShader,
    const D3D11_1DDIARG_STAGE_IO_SIGNATURES* pSignatures
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADER pShader = (PVBOXDXSHADER)hShader.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShader 0x%p, ShaderCode: %X %X ...", pDevice, pShader, pShaderCode[0], pShaderCode[1]));

    pShader->hRTShader = hRTShader;

    vboxDXCreateShader(pDevice, SVGA3D_SHADERTYPE_GS, pShader, pShaderCode,
                       pSignatures->pInputSignature, pSignatures->NumInputSignatureEntries,
                       pSignatures->pOutputSignature, pSignatures->NumOutputSignatureEntries, NULL, 0);
}

static void APIENTRY ddi10CreateGeometryShader(
    D3D10DDI_HDEVICE hDevice,
    const UINT* pShaderCode,
    D3D10DDI_HSHADER hShader,
    D3D10DDI_HRTSHADER hRTShader,
    const D3D10DDIARG_STAGE_IO_SIGNATURES* pSignatures
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADER pShader = (PVBOXDXSHADER)hShader.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShader 0x%p, ShaderCode: %X %X ...", pDevice, pShader, pShaderCode[0], pShaderCode[1]));

    pShader->hRTShader = hRTShader;

    D3D11_1DDIARG_SIGNATURE_ENTRY2 *paInputSignature = NULL;
    D3D11_1DDIARG_SIGNATURE_ENTRY2 *paOutputSignature = NULL;
    D3D11_1DDIARG_SIGNATURE_ENTRY2 *paSignature = NULL;
    uint32_t const cSignatures = pSignatures->NumInputSignatureEntries + pSignatures->NumOutputSignatureEntries;
    if (cSignatures)
    {
        paSignature = (D3D11_1DDIARG_SIGNATURE_ENTRY2 *)RTMemTmpAlloc(cSignatures * (sizeof(D3D11_1DDIARG_SIGNATURE_ENTRY2)));
        AssertReturnVoidStmt(paSignature, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));
        paInputSignature = paSignature;
        paOutputSignature = &paSignature[pSignatures->NumInputSignatureEntries];

        for (unsigned i = 0; i < pSignatures->NumInputSignatureEntries; ++i)
        {
            paInputSignature[i].SystemValue           = pSignatures->pInputSignature[i].SystemValue;
            paInputSignature[i].Register              = pSignatures->pInputSignature[i].Register;
            paInputSignature[i].Mask                  = pSignatures->pInputSignature[i].Mask;
            paInputSignature[i].RegisterComponentType = D3D10_SB_REGISTER_COMPONENT_UNKNOWN;
            paInputSignature[i].MinPrecision          = D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT;
        }

        for (unsigned i = 0; i < pSignatures->NumOutputSignatureEntries; ++i)
        {
            paOutputSignature[i].SystemValue           = pSignatures->pOutputSignature[i].SystemValue;
            paOutputSignature[i].Register              = pSignatures->pOutputSignature[i].Register;
            paOutputSignature[i].Mask                  = pSignatures->pOutputSignature[i].Mask;
            paOutputSignature[i].RegisterComponentType = D3D10_SB_REGISTER_COMPONENT_UNKNOWN;
            paOutputSignature[i].MinPrecision          = D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT;
        }
    }

    vboxDXCreateShader(pDevice, SVGA3D_SHADERTYPE_GS, pShader, pShaderCode,
                       paInputSignature, pSignatures->NumInputSignatureEntries,
                       paOutputSignature, pSignatures->NumOutputSignatureEntries, NULL, 0);
    RTMemTmpFree(paSignature);
}


static void APIENTRY ddi11_1CreatePixelShader(
    D3D10DDI_HDEVICE hDevice,
    const UINT* pShaderCode,
    D3D10DDI_HSHADER hShader,
    D3D10DDI_HRTSHADER hRTShader,
    const D3D11_1DDIARG_STAGE_IO_SIGNATURES* pSignatures
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADER pShader = (PVBOXDXSHADER)hShader.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShader 0x%p, ShaderCode: %X %X ...", pDevice, pShader, pShaderCode[0], pShaderCode[1]));

    pShader->hRTShader = hRTShader;

    vboxDXCreateShader(pDevice, SVGA3D_SHADERTYPE_PS, pShader, pShaderCode,
                       pSignatures->pInputSignature, pSignatures->NumInputSignatureEntries,
                       pSignatures->pOutputSignature, pSignatures->NumOutputSignatureEntries, NULL, 0);
}

static void APIENTRY ddi10CreatePixelShader(
    D3D10DDI_HDEVICE hDevice,
    const UINT* pShaderCode,
    D3D10DDI_HSHADER hShader,
    D3D10DDI_HRTSHADER hRTShader,
    const D3D10DDIARG_STAGE_IO_SIGNATURES* pSignatures
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADER pShader = (PVBOXDXSHADER)hShader.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShader 0x%p, ShaderCode: %X %X ...", pDevice, pShader, pShaderCode[0], pShaderCode[1]));

    pShader->hRTShader = hRTShader;

    D3D11_1DDIARG_SIGNATURE_ENTRY2 *paInputSignature = NULL;
    D3D11_1DDIARG_SIGNATURE_ENTRY2 *paOutputSignature = NULL;
    D3D11_1DDIARG_SIGNATURE_ENTRY2 *paSignature = NULL;
    uint32_t const cSignatures = pSignatures->NumInputSignatureEntries + pSignatures->NumOutputSignatureEntries;
    if (cSignatures)
    {
        paSignature = (D3D11_1DDIARG_SIGNATURE_ENTRY2 *)RTMemTmpAlloc(cSignatures * (sizeof(D3D11_1DDIARG_SIGNATURE_ENTRY2)));
        AssertReturnVoidStmt(paSignature, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));
        paInputSignature = paSignature;
        paOutputSignature = &paSignature[pSignatures->NumInputSignatureEntries];

        for (unsigned i = 0; i < pSignatures->NumInputSignatureEntries; ++i)
        {
            paInputSignature[i].SystemValue           = pSignatures->pInputSignature[i].SystemValue;
            paInputSignature[i].Register              = pSignatures->pInputSignature[i].Register;
            paInputSignature[i].Mask                  = pSignatures->pInputSignature[i].Mask;
            paInputSignature[i].RegisterComponentType = D3D10_SB_REGISTER_COMPONENT_UNKNOWN;
            paInputSignature[i].MinPrecision          = D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT;
        }

        for (unsigned i = 0; i < pSignatures->NumOutputSignatureEntries; ++i)
        {
            paOutputSignature[i].SystemValue           = pSignatures->pOutputSignature[i].SystemValue;
            paOutputSignature[i].Register              = pSignatures->pOutputSignature[i].Register;
            paOutputSignature[i].Mask                  = pSignatures->pOutputSignature[i].Mask;
            paOutputSignature[i].RegisterComponentType = D3D10_SB_REGISTER_COMPONENT_UNKNOWN;
            paOutputSignature[i].MinPrecision          = D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT;
        }
    }

    vboxDXCreateShader(pDevice, SVGA3D_SHADERTYPE_PS, pShader, pShaderCode,
                       paInputSignature, pSignatures->NumInputSignatureEntries,
                       paOutputSignature, pSignatures->NumOutputSignatureEntries, NULL, 0);
    RTMemTmpFree(paSignature);
}


static SIZE_T APIENTRY ddi11_1CalcPrivateGeometryShaderWithStreamOutput(
    D3D10DDI_HDEVICE hDevice,
    const D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT* pCreateGSWithSO,
    const D3D11_1DDIARG_STAGE_IO_SIGNATURES* pSignatures
)
{
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice);
    return sizeof(VBOXDXSHADER)
         + (pCreateGSWithSO->pShaderCode ? pCreateGSWithSO->pShaderCode[1] * sizeof(UINT) : 0)
         + sizeof(SVGA3dDXSignatureHeader)
         + pSignatures->NumInputSignatureEntries * sizeof(SVGA3dDXShaderSignatureEntry)
         + pSignatures->NumOutputSignatureEntries * sizeof(SVGA3dDXShaderSignatureEntry);
}

static SIZE_T APIENTRY ddi11CalcPrivateGeometryShaderWithStreamOutput(
    D3D10DDI_HDEVICE hDevice,
    const D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT* pCreateGSWithSO,
    const D3D10DDIARG_STAGE_IO_SIGNATURES* pSignatures
)
{
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice);
    return sizeof(VBOXDXSHADER)
         + (pCreateGSWithSO->pShaderCode ? pCreateGSWithSO->pShaderCode[1] * sizeof(UINT) : 0)
         + sizeof(SVGA3dDXSignatureHeader)
         + pSignatures->NumInputSignatureEntries * sizeof(SVGA3dDXShaderSignatureEntry)
         + pSignatures->NumOutputSignatureEntries * sizeof(SVGA3dDXShaderSignatureEntry);
}

static SIZE_T APIENTRY ddi10CalcPrivateGeometryShaderWithStreamOutput(
    D3D10DDI_HDEVICE hDevice,
    const D3D10DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT* pCreateGSWithSO,
    const D3D10DDIARG_STAGE_IO_SIGNATURES* pSignatures
)
{
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice);
    return sizeof(VBOXDXSHADER)
         + (pCreateGSWithSO->pShaderCode ? pCreateGSWithSO->pShaderCode[1] * sizeof(UINT) : 0)
         + sizeof(SVGA3dDXSignatureHeader)
         + pSignatures->NumInputSignatureEntries * sizeof(SVGA3dDXShaderSignatureEntry)
         + pSignatures->NumOutputSignatureEntries * sizeof(SVGA3dDXShaderSignatureEntry);
}

static void APIENTRY ddi11_1CreateGeometryShaderWithStreamOutput(
    D3D10DDI_HDEVICE hDevice,
    const D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT* pCreateGSWithSO,
    D3D10DDI_HSHADER hShader,
    D3D10DDI_HRTSHADER hRTShader,
    const D3D11_1DDIARG_STAGE_IO_SIGNATURES* pSignatures
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADER pShader = (PVBOXDXSHADER)hShader.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShader 0x%p, pShaderCode 0x%p", pDevice, pShader, pCreateGSWithSO->pShaderCode));

    pShader->hRTShader = hRTShader;

    vboxDXCreateShader(pDevice, SVGA3D_SHADERTYPE_GS, pShader, pCreateGSWithSO->pShaderCode,
                       pSignatures->pInputSignature, pSignatures->NumInputSignatureEntries,
                       pSignatures->pOutputSignature, pSignatures->NumOutputSignatureEntries, NULL, 0);

    vboxDXCreateStreamOutput(pDevice, pShader, pCreateGSWithSO->pOutputStreamDecl, pCreateGSWithSO->NumEntries,
                             pCreateGSWithSO->BufferStridesInBytes, pCreateGSWithSO->NumStrides,
                             pCreateGSWithSO->RasterizedStream);
}

static void APIENTRY ddi11CreateGeometryShaderWithStreamOutput(
    D3D10DDI_HDEVICE hDevice,
    const D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT* pCreateGSWithSO,
    D3D10DDI_HSHADER hShader,
    D3D10DDI_HRTSHADER hRTShader,
    const D3D10DDIARG_STAGE_IO_SIGNATURES* pSignatures
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADER pShader = (PVBOXDXSHADER)hShader.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShader 0x%p, pShaderCode 0x%p", pDevice, pShader, pCreateGSWithSO->pShaderCode));

    pShader->hRTShader = hRTShader;

    D3D11_1DDIARG_SIGNATURE_ENTRY2 *paInputSignature = NULL;
    D3D11_1DDIARG_SIGNATURE_ENTRY2 *paOutputSignature = NULL;
    D3D11_1DDIARG_SIGNATURE_ENTRY2 *paSignature = NULL;
    uint32_t const cSignatures = pSignatures->NumInputSignatureEntries + pSignatures->NumOutputSignatureEntries;
    if (cSignatures)
    {
        paSignature = (D3D11_1DDIARG_SIGNATURE_ENTRY2 *)RTMemTmpAlloc(cSignatures * (sizeof(D3D11_1DDIARG_SIGNATURE_ENTRY2)));
        AssertReturnVoidStmt(paSignature, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));
        paInputSignature = paSignature;
        paOutputSignature = &paSignature[pSignatures->NumInputSignatureEntries];

        for (unsigned i = 0; i < pSignatures->NumInputSignatureEntries; ++i)
        {
            paInputSignature[i].SystemValue           = pSignatures->pInputSignature[i].SystemValue;
            paInputSignature[i].Register              = pSignatures->pInputSignature[i].Register;
            paInputSignature[i].Mask                  = pSignatures->pInputSignature[i].Mask;
            paInputSignature[i].RegisterComponentType = D3D10_SB_REGISTER_COMPONENT_UNKNOWN;
            paInputSignature[i].MinPrecision          = D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT;
        }

        for (unsigned i = 0; i < pSignatures->NumOutputSignatureEntries; ++i)
        {
            paOutputSignature[i].SystemValue           = pSignatures->pOutputSignature[i].SystemValue;
            paOutputSignature[i].Register              = pSignatures->pOutputSignature[i].Register;
            paOutputSignature[i].Mask                  = pSignatures->pOutputSignature[i].Mask;
            paOutputSignature[i].RegisterComponentType = D3D10_SB_REGISTER_COMPONENT_UNKNOWN;
            paOutputSignature[i].MinPrecision          = D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT;
        }
    }

    vboxDXCreateShader(pDevice, SVGA3D_SHADERTYPE_GS, pShader, pCreateGSWithSO->pShaderCode,
                       paInputSignature, pSignatures->NumInputSignatureEntries,
                       paOutputSignature, pSignatures->NumOutputSignatureEntries, NULL, 0);
    RTMemTmpFree(paSignature);

    vboxDXCreateStreamOutput(pDevice, pShader, pCreateGSWithSO->pOutputStreamDecl, pCreateGSWithSO->NumEntries,
                             pCreateGSWithSO->BufferStridesInBytes, pCreateGSWithSO->NumStrides,
                             pCreateGSWithSO->RasterizedStream);
}

static void APIENTRY ddi10CreateGeometryShaderWithStreamOutput(
    D3D10DDI_HDEVICE hDevice,
    const D3D10DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT* pCreateGSWithSO,
    D3D10DDI_HSHADER hShader,
    D3D10DDI_HRTSHADER hRTShader,
    const D3D10DDIARG_STAGE_IO_SIGNATURES* pSignatures
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADER pShader = (PVBOXDXSHADER)hShader.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShader 0x%p, pShaderCode 0x%p", pDevice, pShader, pCreateGSWithSO->pShaderCode));

    pShader->hRTShader = hRTShader;

    D3D11_1DDIARG_SIGNATURE_ENTRY2 *paInputSignature = NULL;
    D3D11_1DDIARG_SIGNATURE_ENTRY2 *paOutputSignature = NULL;
    D3D11_1DDIARG_SIGNATURE_ENTRY2 *paSignature = NULL;
    uint32_t const cSignatures = pSignatures->NumInputSignatureEntries + pSignatures->NumOutputSignatureEntries;
    if (cSignatures)
    {
        paSignature = (D3D11_1DDIARG_SIGNATURE_ENTRY2 *)RTMemTmpAlloc(cSignatures * (sizeof(D3D11_1DDIARG_SIGNATURE_ENTRY2)));
        AssertReturnVoidStmt(paSignature, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));
        paInputSignature = paSignature;
        paOutputSignature = &paSignature[pSignatures->NumInputSignatureEntries];

        for (unsigned i = 0; i < pSignatures->NumInputSignatureEntries; ++i)
        {
            paInputSignature[i].SystemValue           = pSignatures->pInputSignature[i].SystemValue;
            paInputSignature[i].Register              = pSignatures->pInputSignature[i].Register;
            paInputSignature[i].Mask                  = pSignatures->pInputSignature[i].Mask;
            paInputSignature[i].RegisterComponentType = D3D10_SB_REGISTER_COMPONENT_UNKNOWN;
            paInputSignature[i].MinPrecision          = D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT;
        }

        for (unsigned i = 0; i < pSignatures->NumOutputSignatureEntries; ++i)
        {
            paOutputSignature[i].SystemValue           = pSignatures->pOutputSignature[i].SystemValue;
            paOutputSignature[i].Register              = pSignatures->pOutputSignature[i].Register;
            paOutputSignature[i].Mask                  = pSignatures->pOutputSignature[i].Mask;
            paOutputSignature[i].RegisterComponentType = D3D10_SB_REGISTER_COMPONENT_UNKNOWN;
            paOutputSignature[i].MinPrecision          = D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT;
        }
    }

    vboxDXCreateShader(pDevice, SVGA3D_SHADERTYPE_GS, pShader, pCreateGSWithSO->pShaderCode,
                       paInputSignature, pSignatures->NumInputSignatureEntries,
                       paOutputSignature, pSignatures->NumOutputSignatureEntries, NULL, 0);
    RTMemTmpFree(paSignature);

    D3D11DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY *pOutputStreamDecl = NULL;
    if (pCreateGSWithSO->NumEntries)
    {
        pOutputStreamDecl = (D3D11DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY *)RTMemTmpAlloc(sizeof(D3D11DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY));
        AssertReturnVoidStmt(paSignature, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));

        for (unsigned i = 0; i < pCreateGSWithSO->NumEntries; ++i)
        {
            pOutputStreamDecl[i].Stream        = 0;
            pOutputStreamDecl[i].OutputSlot    = pCreateGSWithSO->pOutputStreamDecl[i].OutputSlot;
            pOutputStreamDecl[i].RegisterIndex = pCreateGSWithSO->pOutputStreamDecl[i].RegisterIndex;
            pOutputStreamDecl[i].RegisterMask  = pCreateGSWithSO->pOutputStreamDecl[i].RegisterMask;
        }
    }

    vboxDXCreateStreamOutput(pDevice, pShader, pOutputStreamDecl, pCreateGSWithSO->NumEntries,
                             &pCreateGSWithSO->StreamOutputStrideInBytes, 1, 0);
    RTMemTmpFree(pOutputStreamDecl);
}

static void APIENTRY ddi10DestroyShader(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADER hShader
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADER pShader = (PVBOXDXSHADER)hShader.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShader 0x%p", pDevice, pShader));

    vboxDXDestroyShader(pDevice, pShader);
}

static SIZE_T APIENTRY ddi10CalcPrivateSamplerSize(
    D3D10DDI_HDEVICE hDevice,
    const D3D10_DDI_SAMPLER_DESC* pSamplerDesc
)
{
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, pSamplerDesc);
    return sizeof(VBOXDX_SAMPLER_STATE);
}

static void APIENTRY ddi10CreateSampler(
    D3D10DDI_HDEVICE hDevice,
    const D3D10_DDI_SAMPLER_DESC* pSamplerDesc,
    D3D10DDI_HSAMPLER hSampler,
    D3D10DDI_HRTSAMPLER hRTSampler
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    VBOXDX_SAMPLER_STATE *pSamplerState = (PVBOXDX_SAMPLER_STATE)hSampler.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, hSampler 0x%p, Filter %d", pDevice, hSampler, pSamplerDesc->Filter));

    pSamplerState->hRTSampler = hRTSampler;
    pSamplerState->SamplerDesc = *pSamplerDesc;

    vboxDXCreateSamplerState(pDevice, pSamplerState);
}

static void APIENTRY ddi10DestroySampler(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSAMPLER hSampler
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    VBOXDX_SAMPLER_STATE *pSamplerState = (PVBOXDX_SAMPLER_STATE)hSampler.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, hSampler 0x%p", pDevice, hSampler));

    vboxDXDestroySamplerState(pDevice, pSamplerState);
}

static SIZE_T APIENTRY ddi10CalcPrivateQuerySize(
    D3D10DDI_HDEVICE hDevice,
    const D3D10DDIARG_CREATEQUERY* pCreateQuery
)
{
    RT_NOREF(hDevice, pCreateQuery);
    return sizeof(VBOXDXQUERY);
}

static void APIENTRY ddi10CreateQuery(
    D3D10DDI_HDEVICE hDevice,
    const D3D10DDIARG_CREATEQUERY* pCreateQuery,
    D3D10DDI_HQUERY hQuery,
    D3D10DDI_HRTQUERY hRTQuery
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXQUERY pQuery = (PVBOXDXQUERY)hQuery.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pQuery 0x%p", pDevice, pQuery));

    pQuery->hRTQuery = hRTQuery;
    vboxDXCreateQuery(pDevice, pQuery, pCreateQuery->Query, pCreateQuery->MiscFlags);
}

static void APIENTRY ddi10DestroyQuery(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HQUERY hQuery
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXQUERY pQuery = (PVBOXDXQUERY)hQuery.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pQuery 0x%p", pDevice, pQuery));

    vboxDXDestroyQuery(pDevice, pQuery);
}

static SVGA3dDevCapIndex vboxDXGIFormat2CapIdx(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: return SVGA3D_DEVCAP_DXFMT_R32G32B32A32_TYPELESS;
    case DXGI_FORMAT_R32G32B32A32_FLOAT:    return SVGA3D_DEVCAP_DXFMT_R32G32B32A32_FLOAT;
    case DXGI_FORMAT_R32G32B32A32_UINT:     return SVGA3D_DEVCAP_DXFMT_R32G32B32A32_UINT;
    case DXGI_FORMAT_R32G32B32A32_SINT:     return SVGA3D_DEVCAP_DXFMT_R32G32B32A32_SINT;

    case DXGI_FORMAT_R32G32B32_TYPELESS:    return SVGA3D_DEVCAP_DXFMT_R32G32B32_TYPELESS;
    case DXGI_FORMAT_R32G32B32_FLOAT:       return SVGA3D_DEVCAP_DXFMT_R32G32B32_FLOAT;
    case DXGI_FORMAT_R32G32B32_UINT:        return SVGA3D_DEVCAP_DXFMT_R32G32B32_UINT;
    case DXGI_FORMAT_R32G32B32_SINT:        return SVGA3D_DEVCAP_DXFMT_R32G32B32_SINT;

    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return SVGA3D_DEVCAP_DXFMT_R16G16B16A16_TYPELESS;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:    return SVGA3D_DEVCAP_DXFMT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R16G16B16A16_UNORM:    return SVGA3D_DEVCAP_DXFMT_R16G16B16A16_UNORM;
    case DXGI_FORMAT_R16G16B16A16_UINT:     return SVGA3D_DEVCAP_DXFMT_R16G16B16A16_UINT;
    case DXGI_FORMAT_R16G16B16A16_SNORM:    return SVGA3D_DEVCAP_DXFMT_R16G16B16A16_SNORM;
    case DXGI_FORMAT_R16G16B16A16_SINT:     return SVGA3D_DEVCAP_DXFMT_R16G16B16A16_SINT;

    case DXGI_FORMAT_R32G32_TYPELESS:       return SVGA3D_DEVCAP_DXFMT_R32G32_TYPELESS;
    case DXGI_FORMAT_R32G32_FLOAT:          return SVGA3D_DEVCAP_DXFMT_R32G32_FLOAT;
    case DXGI_FORMAT_R32G32_UINT:           return SVGA3D_DEVCAP_DXFMT_R32G32_UINT;
    case DXGI_FORMAT_R32G32_SINT:           return SVGA3D_DEVCAP_DXFMT_R32G32_SINT;

    case DXGI_FORMAT_R32G8X24_TYPELESS:     return SVGA3D_DEVCAP_DXFMT_R32G8X24_TYPELESS;

    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:  return SVGA3D_DEVCAP_DXFMT_D32_FLOAT_S8X24_UINT;
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: return SVGA3D_DEVCAP_DXFMT_R32_FLOAT_X8X24;
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:  return SVGA3D_DEVCAP_DXFMT_X32_G8X24_UINT;

    case DXGI_FORMAT_R10G10B10A2_TYPELESS:  return SVGA3D_DEVCAP_DXFMT_R10G10B10A2_TYPELESS;
    case DXGI_FORMAT_R10G10B10A2_UNORM:     return SVGA3D_DEVCAP_DXFMT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R10G10B10A2_UINT:      return SVGA3D_DEVCAP_DXFMT_R10G10B10A2_UINT;

    case DXGI_FORMAT_R11G11B10_FLOAT:       return SVGA3D_DEVCAP_DXFMT_R11G11B10_FLOAT;

    case DXGI_FORMAT_R8G8B8A8_TYPELESS:     return SVGA3D_DEVCAP_DXFMT_R8G8B8A8_TYPELESS;
    case DXGI_FORMAT_R8G8B8A8_UNORM:        return SVGA3D_DEVCAP_DXFMT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:   return SVGA3D_DEVCAP_DXFMT_R8G8B8A8_UNORM_SRGB;
    case DXGI_FORMAT_R8G8B8A8_UINT:         return SVGA3D_DEVCAP_DXFMT_R8G8B8A8_UINT;
    case DXGI_FORMAT_R8G8B8A8_SNORM:        return SVGA3D_DEVCAP_DXFMT_R8G8B8A8_SNORM;
    case DXGI_FORMAT_R8G8B8A8_SINT:         return SVGA3D_DEVCAP_DXFMT_R8G8B8A8_SINT;

    case DXGI_FORMAT_R16G16_TYPELESS:       return SVGA3D_DEVCAP_DXFMT_R16G16_TYPELESS;
    case DXGI_FORMAT_R16G16_FLOAT:          return SVGA3D_DEVCAP_DXFMT_R16G16_FLOAT;
    case DXGI_FORMAT_R16G16_UNORM:          return SVGA3D_DEVCAP_DXFMT_R16G16_UNORM;
    case DXGI_FORMAT_R16G16_UINT:           return SVGA3D_DEVCAP_DXFMT_R16G16_UINT;
    case DXGI_FORMAT_R16G16_SNORM:          return SVGA3D_DEVCAP_DXFMT_R16G16_SNORM;
    case DXGI_FORMAT_R16G16_SINT:           return SVGA3D_DEVCAP_DXFMT_R16G16_SINT;

    case DXGI_FORMAT_R32_TYPELESS:          return SVGA3D_DEVCAP_DXFMT_R32_TYPELESS;
    case DXGI_FORMAT_D32_FLOAT:             return SVGA3D_DEVCAP_DXFMT_D32_FLOAT;
    case DXGI_FORMAT_R32_FLOAT:             return SVGA3D_DEVCAP_DXFMT_R32_FLOAT;
    case DXGI_FORMAT_R32_UINT:              return SVGA3D_DEVCAP_DXFMT_R32_UINT;
    case DXGI_FORMAT_R32_SINT:              return SVGA3D_DEVCAP_DXFMT_R32_SINT;

    case DXGI_FORMAT_R24G8_TYPELESS:        return SVGA3D_DEVCAP_DXFMT_R24G8_TYPELESS;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:     return SVGA3D_DEVCAP_DXFMT_D24_UNORM_S8_UINT;
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: return SVGA3D_DEVCAP_DXFMT_R24_UNORM_X8;
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:  return SVGA3D_DEVCAP_DXFMT_X24_G8_UINT;

    case DXGI_FORMAT_R8G8_TYPELESS:         return SVGA3D_DEVCAP_DXFMT_R8G8_TYPELESS;
    case DXGI_FORMAT_R8G8_UNORM:            return SVGA3D_DEVCAP_DXFMT_R8G8_UNORM;
    case DXGI_FORMAT_R8G8_UINT:             return SVGA3D_DEVCAP_DXFMT_R8G8_UINT;
    case DXGI_FORMAT_R8G8_SNORM:            return SVGA3D_DEVCAP_DXFMT_R8G8_SNORM;
    case DXGI_FORMAT_R8G8_SINT:             return SVGA3D_DEVCAP_DXFMT_R8G8_SINT;

    case DXGI_FORMAT_R16_TYPELESS:          return SVGA3D_DEVCAP_DXFMT_R16_TYPELESS;
    case DXGI_FORMAT_R16_FLOAT:             return SVGA3D_DEVCAP_DXFMT_R16_FLOAT;
    case DXGI_FORMAT_D16_UNORM:             return SVGA3D_DEVCAP_DXFMT_D16_UNORM;
    case DXGI_FORMAT_R16_UNORM:             return SVGA3D_DEVCAP_DXFMT_R16_UNORM;
    case DXGI_FORMAT_R16_UINT:              return SVGA3D_DEVCAP_DXFMT_R16_UINT;
    case DXGI_FORMAT_R16_SNORM:             return SVGA3D_DEVCAP_DXFMT_R16_SNORM;
    case DXGI_FORMAT_R16_SINT:              return SVGA3D_DEVCAP_DXFMT_R16_SINT;

    case DXGI_FORMAT_R8_TYPELESS:           return SVGA3D_DEVCAP_DXFMT_R8_TYPELESS;
    case DXGI_FORMAT_R8_UNORM:              return SVGA3D_DEVCAP_DXFMT_R8_UNORM;
    case DXGI_FORMAT_R8_UINT:               return SVGA3D_DEVCAP_DXFMT_R8_UINT;
    case DXGI_FORMAT_R8_SNORM:              return SVGA3D_DEVCAP_DXFMT_R8_SNORM;
    case DXGI_FORMAT_R8_SINT:               return SVGA3D_DEVCAP_DXFMT_R8_SINT;

    case DXGI_FORMAT_A8_UNORM:              return SVGA3D_DEVCAP_DXFMT_A8_UNORM;
    case DXGI_FORMAT_R1_UNORM:              return SVGA3D_DEVCAP_INVALID;

    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:    return SVGA3D_DEVCAP_DXFMT_R9G9B9E5_SHAREDEXP;
    case DXGI_FORMAT_R8G8_B8G8_UNORM:       return SVGA3D_DEVCAP_DXFMT_R8G8_B8G8_UNORM;
    case DXGI_FORMAT_G8R8_G8B8_UNORM:       return SVGA3D_DEVCAP_DXFMT_G8R8_G8B8_UNORM;

    case DXGI_FORMAT_BC1_TYPELESS:          return SVGA3D_DEVCAP_DXFMT_BC1_TYPELESS;
    case DXGI_FORMAT_BC1_UNORM:             return SVGA3D_DEVCAP_DXFMT_BC1_UNORM;
    case DXGI_FORMAT_BC1_UNORM_SRGB:        return SVGA3D_DEVCAP_DXFMT_BC1_UNORM_SRGB;

    case DXGI_FORMAT_BC2_TYPELESS:    return SVGA3D_DEVCAP_DXFMT_BC2_TYPELESS;
    case DXGI_FORMAT_BC2_UNORM:       return SVGA3D_DEVCAP_DXFMT_BC2_UNORM;
    case DXGI_FORMAT_BC2_UNORM_SRGB:  return SVGA3D_DEVCAP_DXFMT_BC2_UNORM_SRGB;
    case DXGI_FORMAT_BC3_TYPELESS:    return SVGA3D_DEVCAP_DXFMT_BC3_TYPELESS;
    case DXGI_FORMAT_BC3_UNORM:       return SVGA3D_DEVCAP_DXFMT_BC3_UNORM;
    case DXGI_FORMAT_BC3_UNORM_SRGB:  return SVGA3D_DEVCAP_DXFMT_BC3_UNORM_SRGB;
    case DXGI_FORMAT_BC4_TYPELESS:    return SVGA3D_DEVCAP_DXFMT_BC4_TYPELESS;
    case DXGI_FORMAT_BC4_UNORM:       return SVGA3D_DEVCAP_DXFMT_BC4_UNORM;
    case DXGI_FORMAT_BC4_SNORM:       return SVGA3D_DEVCAP_DXFMT_BC4_SNORM;
    case DXGI_FORMAT_BC5_TYPELESS:    return SVGA3D_DEVCAP_DXFMT_BC5_TYPELESS;
    case DXGI_FORMAT_BC5_UNORM:       return SVGA3D_DEVCAP_DXFMT_BC5_UNORM;
    case DXGI_FORMAT_BC5_SNORM:       return SVGA3D_DEVCAP_DXFMT_BC5_SNORM;

    case DXGI_FORMAT_B5G6R5_UNORM:    return SVGA3D_DEVCAP_DXFMT_B5G6R5_UNORM;
    case DXGI_FORMAT_B5G5R5A1_UNORM:  return SVGA3D_DEVCAP_DXFMT_B5G5R5A1_UNORM;
    case DXGI_FORMAT_B8G8R8A8_UNORM:  return SVGA3D_DEVCAP_DXFMT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_UNORM:  return SVGA3D_DEVCAP_DXFMT_B8G8R8X8_UNORM;

    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM: return SVGA3D_DEVCAP_DXFMT_R10G10B10_XR_BIAS_A2_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:   return SVGA3D_DEVCAP_DXFMT_B8G8R8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return SVGA3D_DEVCAP_DXFMT_B8G8R8A8_UNORM_SRGB;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:   return SVGA3D_DEVCAP_DXFMT_B8G8R8X8_TYPELESS;
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return SVGA3D_DEVCAP_DXFMT_B8G8R8X8_UNORM_SRGB;

    case DXGI_FORMAT_BC6H_TYPELESS:  return SVGA3D_DEVCAP_INVALID;
    case DXGI_FORMAT_BC6H_UF16:      return SVGA3D_DEVCAP_INVALID;
    case DXGI_FORMAT_BC6H_SF16:      return SVGA3D_DEVCAP_INVALID;
    case DXGI_FORMAT_BC7_TYPELESS:   return SVGA3D_DEVCAP_INVALID;
    case DXGI_FORMAT_BC7_UNORM:      return SVGA3D_DEVCAP_INVALID;
    case DXGI_FORMAT_BC7_UNORM_SRGB: return SVGA3D_DEVCAP_INVALID;

    case DXGI_FORMAT_AYUV:           return SVGA3D_DEVCAP_DXFMT_FORMAT_DEAD2; /* Was SVGA3D_DEVCAP_DXFMT_AYUV */
    case DXGI_FORMAT_Y410:           return SVGA3D_DEVCAP_INVALID;
    case DXGI_FORMAT_Y416:           return SVGA3D_DEVCAP_INVALID;
    case DXGI_FORMAT_NV12:           return SVGA3D_DEVCAP_DXFMT_NV12;
    case DXGI_FORMAT_P010:           return SVGA3D_DEVCAP_INVALID;
    case DXGI_FORMAT_P016:           return SVGA3D_DEVCAP_INVALID;
    case DXGI_FORMAT_420_OPAQUE:     return SVGA3D_DEVCAP_INVALID;
    case DXGI_FORMAT_YUY2:           return SVGA3D_DEVCAP_DXFMT_YUY2;
    case DXGI_FORMAT_Y210:           return SVGA3D_DEVCAP_INVALID;
    case DXGI_FORMAT_Y216:           return SVGA3D_DEVCAP_INVALID;
    case DXGI_FORMAT_NV11:           return SVGA3D_DEVCAP_INVALID;
    case DXGI_FORMAT_AI44:           return SVGA3D_DEVCAP_INVALID;
    case DXGI_FORMAT_IA44:           return SVGA3D_DEVCAP_INVALID;
    case DXGI_FORMAT_P8:             return SVGA3D_DEVCAP_DXFMT_P8;
    case DXGI_FORMAT_A8P8:           return SVGA3D_DEVCAP_INVALID;
    case DXGI_FORMAT_B4G4R4A4_UNORM: return SVGA3D_DEVCAP_INVALID;

    default:
        break;
    }

    return SVGA3D_DEVCAP_INVALID;
}

void APIENTRY vboxDXCheckFormatSupport(
    D3D10DDI_HDEVICE hDevice,
    DXGI_FORMAT Format,
    UINT* pFormatCaps
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXADAPTER pVBoxAdapter = pDevice->pAdapter;
    LogFlowFunc(("pDevice 0x%p, Format %d", pDevice, Format));
    SVGA3dDevCapIndex idxDevCap = vboxDXGIFormat2CapIdx(Format);

    *pFormatCaps = 0;

    if (idxDevCap != SVGA3D_DEVCAP_INVALID)
    {
        uint32_t *au32Caps = pVBoxAdapter->AdapterInfo.u.vmsvga.HWInfo.u.svga.au32Caps;
        uint32_t u32Cap = au32Caps[idxDevCap];
        LogFlowFunc(("DXGI Format %d is SVGA %d, caps 0x%X", Format, idxDevCap, u32Cap));

        if (u32Cap & SVGA3D_DXFMT_SUPPORTED)
        {
            if (u32Cap & SVGA3D_DXFMT_SHADER_SAMPLE)
                *pFormatCaps |= D3D10_DDI_FORMAT_SUPPORT_SHADER_SAMPLE;

            if (u32Cap & SVGA3D_DXFMT_COLOR_RENDERTARGET)
                *pFormatCaps |= D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET;

            if (u32Cap & SVGA3D_DXFMT_BLENDABLE)
                *pFormatCaps |= D3D10_DDI_FORMAT_SUPPORT_BLENDABLE;

            if (u32Cap & SVGA3D_DXFMT_DX_VERTEX_BUFFER)
                *pFormatCaps |= D3D11_1DDI_FORMAT_SUPPORT_VERTEX_BUFFER;

            /* The SVGA values below do not have exact equivalents in DX11 (that is strange).
            https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d10umddi/nc-d3d10umddi-pfnd3d10ddi_checkformatsupport

            if (u32Cap & SVGA3D_DXFMT_DEPTH_RENDERTARGET)
            if (u32Cap & SVGA3D_DXFMT_MIPS)
            if (u32Cap & SVGA3D_DXFMT_ARRAY)
            if (u32Cap & SVGA3D_DXFMT_VOLUME)*/

            if (u32Cap & SVGA3D_DXFMT_MULTISAMPLE)
                *pFormatCaps |= D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET;
        }
    } else {
        LogFlowFunc(("Format %d is not supported", Format));
    }
}

void APIENTRY vboxDXCheckMultisampleQualityLevels(
    D3D10DDI_HDEVICE hDevice,
    DXGI_FORMAT Format,
    UINT SampleCount,
    UINT* pNumQualityLevels
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    //LogFlowFunc(("pDevice 0x%p, Format %d, SampleCount %d", pDevice, Format, SampleCount));

    if (SampleCount == 1)
        *pNumQualityLevels = 1;
    else
    {
        *pNumQualityLevels = 0;

        SVGA3dDevCapIndex const idxDevCap = vboxDXGIFormat2CapIdx(Format);
        if (idxDevCap != SVGA3D_DEVCAP_INVALID)
        {
            PVBOXDXADAPTER pVBoxAdapter = pDevice->pAdapter;
            uint32_t const *au32Caps = pVBoxAdapter->AdapterInfo.u.vmsvga.HWInfo.u.svga.au32Caps;
            uint32_t const u32Cap = au32Caps[idxDevCap];
            if (u32Cap & SVGA3D_DXFMT_MULTISAMPLE)
            {
                bool fSampleCountSupported = false;
                if (SampleCount == 2)
                   fSampleCountSupported = RT_BOOL(au32Caps[SVGA3D_DEVCAP_MULTISAMPLE_2X]);
                else if (SampleCount == 4)
                   fSampleCountSupported = RT_BOOL(au32Caps[SVGA3D_DEVCAP_MULTISAMPLE_4X]);
                else if (SampleCount == 8)
                   fSampleCountSupported = RT_BOOL(au32Caps[SVGA3D_DEVCAP_MULTISAMPLE_8X]);
                *pNumQualityLevels = fSampleCountSupported ? 1 : 0;
            }
        }
    }
}

static void APIENTRY ddi10CheckCounterInfo(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_COUNTER_INFO* pCounterInfo
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    RT_NOREF(pDevice);

    /* No "device-dependent" counters. */
    RT_ZERO(*pCounterInfo);
}

static void APIENTRY ddi10CheckCounter(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_QUERY Query,
    D3D10DDI_COUNTER_TYPE* pCounterType,
    UINT* pActiveCounters,
    LPSTR pDescription,
    UINT* pNameLength,
    LPSTR pName,
    UINT* pUnitsLength,
    LPSTR pUnits,
    UINT* pDescriptionLength
)
{
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, Query, pCounterType, pActiveCounters, pDescription, pNameLength, pName,
        pUnitsLength, pUnits, pDescriptionLength);
    /* No "device-dependent" counters. */
}

static void APIENTRY ddi10SetTextFilterSize(
    D3D10DDI_HDEVICE hDevice,
    UINT Width,
    UINT Height
)
{
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, Width, Height);
    LogFlowFunc(("%dx%d", Width, Height));
    /* Not implemented  because "text filtering D3D10_FILTER_TEXT_1BIT was removed from Direct3D 11" */
}

void APIENTRY vboxDXResourceConvert(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDstResource,
    D3D10DDI_HRESOURCE hSrcResource
)
{
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, hDstResource, hSrcResource);
    LogFlowFuncEnter();
}

void APIENTRY vboxDXResourceConvertRegion(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDstResource,
    UINT DstSubresource,
    UINT DstX,
    UINT DstY,
    UINT DstZ,
    D3D10DDI_HRESOURCE hSrcResource,
    UINT SrcSubresource,
    const D3D10_DDI_BOX* pSrcBox,
    UINT CopyFlags
)
{
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, hDstResource, DstSubresource, DstX, DstY, DstZ, hSrcResource, SrcSubresource, pSrcBox, CopyFlags);
    LogFlowFuncEnter();
}

static void APIENTRY ddi10ResourceConvertRegion(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDstResource,
    UINT DstSubresource,
    UINT DstX,
    UINT DstY,
    UINT DstZ,
    D3D10DDI_HRESOURCE hSrcResource,
    UINT SrcSubresource,
    const D3D10_DDI_BOX* pSrcBox
)
{
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, hDstResource, DstSubresource, DstX, DstY, DstZ, hSrcResource, SrcSubresource, pSrcBox);
    LogFlowFuncEnter();
}

static void APIENTRY ddi11DrawIndexedInstancedIndirect(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hBufferForArgs,
    UINT AlignedByteOffsetForArgs
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)hBufferForArgs.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pResource %p, AlignedByteOffsetForArgs %u", pDevice, pResource, AlignedByteOffsetForArgs));

    vboxDXDrawIndexedInstancedIndirect(pDevice, pResource, AlignedByteOffsetForArgs);
}

static void APIENTRY ddi11DrawInstancedIndirect(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hBufferForArgs,
    UINT AlignedByteOffsetForArgs
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)hBufferForArgs.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pResource %p, AlignedByteOffsetForArgs %u", pDevice, pResource, AlignedByteOffsetForArgs));

    vboxDXDrawInstancedIndirect(pDevice, pResource, AlignedByteOffsetForArgs);
}

static void APIENTRY ddi10HsSetShaderResources(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot,
    UINT NumViews,
    const D3D10DDI_HSHADERRESOURCEVIEW* phShaderResourceViews
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumViews = %u\n", pDevice, StartSlot, NumViews));

    vboxDXSetShaderResourceViews(pDevice, SVGA3D_SHADERTYPE_HS, StartSlot, NumViews, (PVBOXDXSHADERRESOURCEVIEW *)phShaderResourceViews);
}

static void APIENTRY ddi10HsSetShader(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADER hShader
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADER pShader = (PVBOXDXSHADER)hShader.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShader 0x%p", pDevice, pShader));

    vboxDXSetShader(pDevice, SVGA3D_SHADERTYPE_HS, pShader);
}

static void APIENTRY ddi10HsSetSamplers(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot,
    UINT NumSamplers,
    const D3D10DDI_HSAMPLER* phSamplers
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumSamplers = %u\n", pDevice, StartSlot, NumSamplers));

    Assert(NumSamplers <= SVGA3D_DX_MAX_SAMPLERS);
    NumSamplers = RT_MIN(NumSamplers, SVGA3D_DX_MAX_SAMPLERS);

    /* Fetch sampler ids. */
    uint32_t aSamplerIds[SVGA3D_DX_MAX_SAMPLERS];
    for (unsigned i = 0; i < NumSamplers; ++i)
    {
        VBOXDX_SAMPLER_STATE *pSamplerState = (PVBOXDX_SAMPLER_STATE)phSamplers[i].pDrvPrivate;
        aSamplerIds[i] = pSamplerState ? pSamplerState->uSamplerId : SVGA3D_INVALID_ID;
    }

    vboxDXSetSamplers(pDevice, SVGA3D_SHADERTYPE_HS, StartSlot, NumSamplers, aSamplerIds);
}

static void APIENTRY ddi11_1HsSetConstantBuffers(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot,
    UINT NumBuffers,
    const D3D10DDI_HRESOURCE* phBuffers,
    const UINT* pFirstConstant,
    const UINT* pNumConstants
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumBuffers = %u\n", pDevice, StartSlot, NumBuffers));

    vboxDXSetConstantBuffers(pDevice, SVGA3D_SHADERTYPE_HS, StartSlot, NumBuffers, (PVBOXDX_RESOURCE *)phBuffers, pFirstConstant, pNumConstants);
}

static void APIENTRY ddi10HsSetConstantBuffers(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot,             // The starting constant buffer to set.
    UINT NumBuffers,            // The total number of buffers to set.
    const D3D10DDI_HRESOURCE* phBuffers // An array of handles to the constant buffers, beginning with the buffer that StartBuffer specifies.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumBuffers = %u\n", pDevice, StartSlot, NumBuffers));

    vboxDXSetConstantBuffers(pDevice, SVGA3D_SHADERTYPE_HS, StartSlot, NumBuffers, (PVBOXDX_RESOURCE *)phBuffers, NULL, NULL);
}

static void APIENTRY ddi10DsSetShaderResources(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot,
    UINT NumViews,
    const D3D10DDI_HSHADERRESOURCEVIEW* phShaderResourceViews
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumViews = %u\n", pDevice, StartSlot, NumViews));

    vboxDXSetShaderResourceViews(pDevice, SVGA3D_SHADERTYPE_DS, StartSlot, NumViews, (PVBOXDXSHADERRESOURCEVIEW *)phShaderResourceViews);
}

static void APIENTRY ddi10DsSetShader(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADER hShader
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADER pShader = (PVBOXDXSHADER)hShader.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShader 0x%p", pDevice, pShader));

    vboxDXSetShader(pDevice, SVGA3D_SHADERTYPE_DS, pShader);
}

static void APIENTRY ddi10DsSetSamplers(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot,
    UINT NumSamplers,
    const D3D10DDI_HSAMPLER* phSamplers
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumSamplers = %u\n", pDevice, StartSlot, NumSamplers));

    Assert(NumSamplers <= SVGA3D_DX_MAX_SAMPLERS);
    NumSamplers = RT_MIN(NumSamplers, SVGA3D_DX_MAX_SAMPLERS);

    /* Fetch sampler ids. */
    uint32_t aSamplerIds[SVGA3D_DX_MAX_SAMPLERS];
    for (unsigned i = 0; i < NumSamplers; ++i)
    {
        VBOXDX_SAMPLER_STATE *pSamplerState = (PVBOXDX_SAMPLER_STATE)phSamplers[i].pDrvPrivate;
        aSamplerIds[i] = pSamplerState ? pSamplerState->uSamplerId : SVGA3D_INVALID_ID;
    }

    vboxDXSetSamplers(pDevice, SVGA3D_SHADERTYPE_DS, StartSlot, NumSamplers, aSamplerIds);
}

static void APIENTRY ddi11_1DsSetConstantBuffers(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot,
    UINT NumBuffers,
    const D3D10DDI_HRESOURCE* phBuffers,
    const UINT* pFirstConstant,
    const UINT* pNumConstants
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumBuffers = %u\n", pDevice, StartSlot, NumBuffers));

    vboxDXSetConstantBuffers(pDevice, SVGA3D_SHADERTYPE_DS, StartSlot, NumBuffers, (PVBOXDX_RESOURCE *)phBuffers, pFirstConstant, pNumConstants);
}

static void APIENTRY ddi10DsSetConstantBuffers(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot,             // The starting constant buffer to set.
    UINT NumBuffers,            // The total number of buffers to set.
    const D3D10DDI_HRESOURCE* phBuffers // An array of handles to the constant buffers, beginning with the buffer that StartBuffer specifies.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumBuffers = %u\n", pDevice, StartSlot, NumBuffers));

    vboxDXSetConstantBuffers(pDevice, SVGA3D_SHADERTYPE_DS, StartSlot, NumBuffers, (PVBOXDX_RESOURCE *)phBuffers, NULL, NULL);
}

static void APIENTRY ddi11_1CreateHullShader(
    D3D10DDI_HDEVICE hDevice,
    const UINT* pShaderCode,
    D3D10DDI_HSHADER hShader,
    D3D10DDI_HRTSHADER hRTShader,
    const D3D11_1DDIARG_TESSELLATION_IO_SIGNATURES* pSignatures
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADER pShader = (PVBOXDXSHADER)hShader.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShader 0x%p, ShaderCode: %X %X ...", pDevice, pShader, pShaderCode[0], pShaderCode[1]));

    pShader->hRTShader = hRTShader;

    vboxDXCreateShader(pDevice, SVGA3D_SHADERTYPE_HS, pShader, pShaderCode,
                       pSignatures->pInputSignature, pSignatures->NumInputSignatureEntries,
                       pSignatures->pOutputSignature, pSignatures->NumOutputSignatureEntries,
                       pSignatures->pPatchConstantSignature, pSignatures->NumPatchConstantSignatureEntries);
}

static void APIENTRY ddi11CreateHullShader(
    D3D10DDI_HDEVICE hDevice,
    const UINT* pShaderCode,
    D3D10DDI_HSHADER hShader,
    D3D10DDI_HRTSHADER hRTShader,
    const D3D11DDIARG_TESSELLATION_IO_SIGNATURES* pSignatures
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADER pShader = (PVBOXDXSHADER)hShader.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShader 0x%p, ShaderCode: %X %X ...", pDevice, pShader, pShaderCode[0], pShaderCode[1]));

    pShader->hRTShader = hRTShader;

    D3D11_1DDIARG_SIGNATURE_ENTRY2 *paInputSignature = NULL;
    D3D11_1DDIARG_SIGNATURE_ENTRY2 *paOutputSignature = NULL;
    D3D11_1DDIARG_SIGNATURE_ENTRY2 *paPatchConstantSignature = NULL;
    D3D11_1DDIARG_SIGNATURE_ENTRY2 *paSignature = NULL;
    uint32_t const cSignatures = pSignatures->NumInputSignatureEntries + pSignatures->NumOutputSignatureEntries + pSignatures->NumPatchConstantSignatureEntries;
    if (cSignatures)
    {
        paSignature = (D3D11_1DDIARG_SIGNATURE_ENTRY2 *)RTMemTmpAlloc(cSignatures * (sizeof(D3D11_1DDIARG_SIGNATURE_ENTRY2)));
        AssertReturnVoidStmt(paSignature, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));
        paInputSignature = paSignature;
        paOutputSignature = &paInputSignature[pSignatures->NumInputSignatureEntries];
        paPatchConstantSignature = &paOutputSignature[pSignatures->NumOutputSignatureEntries];

        for (unsigned i = 0; i < pSignatures->NumInputSignatureEntries; ++i)
        {
            paInputSignature[i].SystemValue           = pSignatures->pInputSignature[i].SystemValue;
            paInputSignature[i].Register              = pSignatures->pInputSignature[i].Register;
            paInputSignature[i].Mask                  = pSignatures->pInputSignature[i].Mask;
            paInputSignature[i].RegisterComponentType = D3D10_SB_REGISTER_COMPONENT_UNKNOWN;
            paInputSignature[i].MinPrecision          = D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT;
        }

        for (unsigned i = 0; i < pSignatures->NumOutputSignatureEntries; ++i)
        {
            paOutputSignature[i].SystemValue           = pSignatures->pOutputSignature[i].SystemValue;
            paOutputSignature[i].Register              = pSignatures->pOutputSignature[i].Register;
            paOutputSignature[i].Mask                  = pSignatures->pOutputSignature[i].Mask;
            paOutputSignature[i].RegisterComponentType = D3D10_SB_REGISTER_COMPONENT_UNKNOWN;
            paOutputSignature[i].MinPrecision          = D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT;
        }

        for (unsigned i = 0; i < pSignatures->NumPatchConstantSignatureEntries; ++i)
        {
            paPatchConstantSignature[i].SystemValue           = pSignatures->pPatchConstantSignature[i].SystemValue;
            paPatchConstantSignature[i].Register              = pSignatures->pPatchConstantSignature[i].Register;
            paPatchConstantSignature[i].Mask                  = pSignatures->pPatchConstantSignature[i].Mask;
            paPatchConstantSignature[i].RegisterComponentType = D3D10_SB_REGISTER_COMPONENT_UNKNOWN;
            paPatchConstantSignature[i].MinPrecision          = D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT;
        }
    }

    vboxDXCreateShader(pDevice, SVGA3D_SHADERTYPE_HS, pShader, pShaderCode,
                       paInputSignature, pSignatures->NumInputSignatureEntries,
                       paOutputSignature, pSignatures->NumOutputSignatureEntries,
                       paPatchConstantSignature, pSignatures->NumPatchConstantSignatureEntries);
    RTMemTmpFree(paSignature);
}

static void APIENTRY ddi11_1CreateDomainShader(
    D3D10DDI_HDEVICE hDevice,
    const UINT* pShaderCode,
    D3D10DDI_HSHADER hShader,
    D3D10DDI_HRTSHADER hRTShader,
    const D3D11_1DDIARG_TESSELLATION_IO_SIGNATURES* pSignatures
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADER pShader = (PVBOXDXSHADER)hShader.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShader 0x%p, ShaderCode: %X %X ...", pDevice, pShader, pShaderCode[0], pShaderCode[1]));

    pShader->hRTShader = hRTShader;

    vboxDXCreateShader(pDevice, SVGA3D_SHADERTYPE_DS, pShader, pShaderCode,
                       pSignatures->pInputSignature, pSignatures->NumInputSignatureEntries,
                       pSignatures->pOutputSignature, pSignatures->NumOutputSignatureEntries,
                       pSignatures->pPatchConstantSignature, pSignatures->NumPatchConstantSignatureEntries);
}

static void APIENTRY ddi11CreateDomainShader(
    D3D10DDI_HDEVICE hDevice,
    const UINT* pShaderCode,
    D3D10DDI_HSHADER hShader,
    D3D10DDI_HRTSHADER hRTShader,
    const D3D11DDIARG_TESSELLATION_IO_SIGNATURES* pSignatures
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADER pShader = (PVBOXDXSHADER)hShader.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShader 0x%p, ShaderCode: %X %X ...", pDevice, pShader, pShaderCode[0], pShaderCode[1]));

    pShader->hRTShader = hRTShader;

    D3D11_1DDIARG_SIGNATURE_ENTRY2 *paInputSignature = NULL;
    D3D11_1DDIARG_SIGNATURE_ENTRY2 *paOutputSignature = NULL;
    D3D11_1DDIARG_SIGNATURE_ENTRY2 *paPatchConstantSignature = NULL;
    D3D11_1DDIARG_SIGNATURE_ENTRY2 *paSignature = NULL;
    uint32_t const cSignatures = pSignatures->NumInputSignatureEntries + pSignatures->NumOutputSignatureEntries + pSignatures->NumPatchConstantSignatureEntries;
    if (cSignatures)
    {
        paSignature = (D3D11_1DDIARG_SIGNATURE_ENTRY2 *)RTMemTmpAlloc(cSignatures * (sizeof(D3D11_1DDIARG_SIGNATURE_ENTRY2)));
        AssertReturnVoidStmt(paSignature, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY));
        paInputSignature = paSignature;
        paOutputSignature = &paInputSignature[pSignatures->NumInputSignatureEntries];
        paPatchConstantSignature = &paOutputSignature[pSignatures->NumOutputSignatureEntries];

        for (unsigned i = 0; i < pSignatures->NumInputSignatureEntries; ++i)
        {
            paInputSignature[i].SystemValue           = pSignatures->pInputSignature[i].SystemValue;
            paInputSignature[i].Register              = pSignatures->pInputSignature[i].Register;
            paInputSignature[i].Mask                  = pSignatures->pInputSignature[i].Mask;
            paInputSignature[i].RegisterComponentType = D3D10_SB_REGISTER_COMPONENT_UNKNOWN;
            paInputSignature[i].MinPrecision          = D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT;
        }

        for (unsigned i = 0; i < pSignatures->NumOutputSignatureEntries; ++i)
        {
            paOutputSignature[i].SystemValue           = pSignatures->pOutputSignature[i].SystemValue;
            paOutputSignature[i].Register              = pSignatures->pOutputSignature[i].Register;
            paOutputSignature[i].Mask                  = pSignatures->pOutputSignature[i].Mask;
            paOutputSignature[i].RegisterComponentType = D3D10_SB_REGISTER_COMPONENT_UNKNOWN;
            paOutputSignature[i].MinPrecision          = D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT;
        }

        for (unsigned i = 0; i < pSignatures->NumPatchConstantSignatureEntries; ++i)
        {
            paPatchConstantSignature[i].SystemValue           = pSignatures->pPatchConstantSignature[i].SystemValue;
            paPatchConstantSignature[i].Register              = pSignatures->pPatchConstantSignature[i].Register;
            paPatchConstantSignature[i].Mask                  = pSignatures->pPatchConstantSignature[i].Mask;
            paPatchConstantSignature[i].RegisterComponentType = D3D10_SB_REGISTER_COMPONENT_UNKNOWN;
            paPatchConstantSignature[i].MinPrecision          = D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT;
        }
    }

    vboxDXCreateShader(pDevice, SVGA3D_SHADERTYPE_DS, pShader, pShaderCode,
                       paInputSignature, pSignatures->NumInputSignatureEntries,
                       paOutputSignature, pSignatures->NumOutputSignatureEntries,
                       paPatchConstantSignature, pSignatures->NumPatchConstantSignatureEntries);
    RTMemTmpFree(paSignature);
}

static SIZE_T APIENTRY ddi11_1CalcPrivateTessellationShaderSize(
    D3D10DDI_HDEVICE hDevice,
    const UINT* pShaderCode,
    const D3D11_1DDIARG_TESSELLATION_IO_SIGNATURES* pSignatures
)
{
    RT_NOREF(hDevice);
    return sizeof(VBOXDXSHADER)
         + pShaderCode[1] * sizeof(UINT)
         + sizeof(SVGA3dDXSignatureHeader)
         + pSignatures->NumInputSignatureEntries * sizeof(SVGA3dDXShaderSignatureEntry)
         + pSignatures->NumOutputSignatureEntries * sizeof(SVGA3dDXShaderSignatureEntry)
         + pSignatures->NumPatchConstantSignatureEntries * sizeof(SVGA3dDXShaderSignatureEntry);
}

static SIZE_T APIENTRY ddi11CalcPrivateTessellationShaderSize(
    D3D10DDI_HDEVICE hDevice,
    const UINT* pShaderCode,
    const D3D11DDIARG_TESSELLATION_IO_SIGNATURES* pSignatures
)
{
    RT_NOREF(hDevice);
    return sizeof(VBOXDXSHADER)
         + pShaderCode[1] * sizeof(UINT)
         + sizeof(SVGA3dDXSignatureHeader)
         + pSignatures->NumInputSignatureEntries * sizeof(SVGA3dDXShaderSignatureEntry)
         + pSignatures->NumOutputSignatureEntries * sizeof(SVGA3dDXShaderSignatureEntry)
         + pSignatures->NumPatchConstantSignatureEntries * sizeof(SVGA3dDXShaderSignatureEntry);
}

void APIENTRY vboxDXPsSetShaderWithIfaces(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADER hShader,
    UINT NumClassInstances,
    const UINT* pPointerData,
    const D3D11DDIARG_POINTERDATA* pIfaces
)
{
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, hShader, NumClassInstances, pPointerData, pIfaces);
    LogFlowFuncEnter();
}

void APIENTRY vboxDXVsSetShaderWithIfaces(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADER hShader,
    UINT NumClassInstances,
    const UINT* pPointerData,
    const D3D11DDIARG_POINTERDATA* pIfaces
)
{
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, hShader, NumClassInstances, pPointerData, pIfaces);
    LogFlowFuncEnter();
}

void APIENTRY vboxDXGsSetShaderWithIfaces(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADER hShader,
    UINT NumClassInstances,
    const UINT* pPointerData,
    const D3D11DDIARG_POINTERDATA* pIfaces
)
{
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, hShader, NumClassInstances, pPointerData, pIfaces);
    LogFlowFuncEnter();
}

void APIENTRY vboxDXHsSetShaderWithIfaces(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADER hShader,
    UINT NumClassInstances,
    const UINT* pPointerData,
    const D3D11DDIARG_POINTERDATA* pIfaces
)
{
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, hShader, NumClassInstances, pPointerData, pIfaces);
    LogFlowFuncEnter();
}

void APIENTRY vboxDXDsSetShaderWithIfaces(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADER hShader,
    UINT NumClassInstances,
    const UINT* pPointerData,
    const D3D11DDIARG_POINTERDATA* pIfaces
)
{
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, hShader, NumClassInstances, pPointerData, pIfaces);
    LogFlowFuncEnter();
}

void APIENTRY vboxDXCsSetShaderWithIfaces(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADER hShader,
    UINT NumClassInstances,
    const UINT* pPointerData,
    const D3D11DDIARG_POINTERDATA* pIfaces
)
{
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, hShader, NumClassInstances, pPointerData, pIfaces);
    LogFlowFuncEnter();
}

static void APIENTRY ddi11CreateComputeShader(
    D3D10DDI_HDEVICE hDevice,
    const UINT* pShaderCode,
    D3D10DDI_HSHADER hShader,
    D3D10DDI_HRTSHADER hRTShader
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADER pShader = (PVBOXDXSHADER)hShader.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShader 0x%p, ShaderCode: %X %X ...", pDevice, pShader, pShaderCode[0], pShaderCode[1]));

    pShader->hRTShader = hRTShader;

    vboxDXCreateShader(pDevice, SVGA3D_SHADERTYPE_CS, pShader, pShaderCode, NULL, 0, NULL, 0, NULL, 0);
}

static void APIENTRY ddi10CsSetShader(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADER hShader
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADER pShader = (PVBOXDXSHADER)hShader.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShader 0x%p", pDevice, pShader));

    vboxDXSetShader(pDevice, SVGA3D_SHADERTYPE_CS, pShader);
}

static void APIENTRY ddi10CsSetShaderResources(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot,
    UINT NumViews,
    const D3D10DDI_HSHADERRESOURCEVIEW* phShaderResourceViews
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumViews = %u\n", pDevice, StartSlot, NumViews));

    vboxDXSetShaderResourceViews(pDevice, SVGA3D_SHADERTYPE_CS, StartSlot, NumViews, (PVBOXDXSHADERRESOURCEVIEW *)phShaderResourceViews);
}

static void APIENTRY ddi10CsSetSamplers(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot,
    UINT NumSamplers,
    const D3D10DDI_HSAMPLER* phSamplers
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumSamplers = %u\n", pDevice, StartSlot, NumSamplers));

    Assert(NumSamplers <= SVGA3D_DX_MAX_SAMPLERS);
    NumSamplers = RT_MIN(NumSamplers, SVGA3D_DX_MAX_SAMPLERS);

    /* Fetch sampler ids. */
    uint32_t aSamplerIds[SVGA3D_DX_MAX_SAMPLERS];
    for (unsigned i = 0; i < NumSamplers; ++i)
    {
        VBOXDX_SAMPLER_STATE *pSamplerState = (PVBOXDX_SAMPLER_STATE)phSamplers[i].pDrvPrivate;
        aSamplerIds[i] = pSamplerState ? pSamplerState->uSamplerId : SVGA3D_INVALID_ID;
    }

    vboxDXSetSamplers(pDevice, SVGA3D_SHADERTYPE_CS, StartSlot, NumSamplers, aSamplerIds);
}

static void APIENTRY ddi11_1CsSetConstantBuffers(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot,
    UINT NumBuffers,
    const D3D10DDI_HRESOURCE* phBuffers,
    const UINT* pFirstConstant,
    const UINT* pNumConstants
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumBuffers = %u\n", pDevice, StartSlot, NumBuffers));

    vboxDXSetConstantBuffers(pDevice, SVGA3D_SHADERTYPE_CS, StartSlot, NumBuffers, (PVBOXDX_RESOURCE *)phBuffers, pFirstConstant, pNumConstants);
}

static void APIENTRY ddi10CsSetConstantBuffers(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot,             // The starting constant buffer to set.
    UINT NumBuffers,            // The total number of buffers to set.
    const D3D10DDI_HRESOURCE* phBuffers // An array of handles to the constant buffers, beginning with the buffer that StartBuffer specifies.
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumBuffers = %u\n", pDevice, StartSlot, NumBuffers));

    vboxDXSetConstantBuffers(pDevice, SVGA3D_SHADERTYPE_CS, StartSlot, NumBuffers, (PVBOXDX_RESOURCE *)phBuffers, NULL, NULL);
}

static SIZE_T APIENTRY ddi11CalcPrivateUnorderedAccessViewSize(
    D3D10DDI_HDEVICE hDevice,
    const D3D11DDIARG_CREATEUNORDEREDACCESSVIEW* pCreateUnorderedAccessView
)
{
    RT_NOREF(hDevice, pCreateUnorderedAccessView);
    return sizeof(VBOXDXUNORDEREDACCESSVIEW);
}

static void APIENTRY ddi11CreateUnorderedAccessView(
    D3D10DDI_HDEVICE hDevice,
    const D3D11DDIARG_CREATEUNORDEREDACCESSVIEW* pCreateUnorderedAccessView,
    D3D11DDI_HUNORDEREDACCESSVIEW hUnorderedAccessView,
    D3D11DDI_HRTUNORDEREDACCESSVIEW hRTUnorderedAccessView
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)pCreateUnorderedAccessView->hDrvResource.pDrvPrivate;
    PVBOXDXUNORDEREDACCESSVIEW pUnorderedAccessView = (PVBOXDXUNORDEREDACCESSVIEW)hUnorderedAccessView.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pResource %p, pUnorderedAccessView 0x%p", pDevice, pResource, pUnorderedAccessView));

    pUnorderedAccessView->hRTUnorderedAccessView = hRTUnorderedAccessView;
    pUnorderedAccessView->pResource = pResource;
    pUnorderedAccessView->Format = pCreateUnorderedAccessView->Format;
    pUnorderedAccessView->ResourceDimension = pCreateUnorderedAccessView->ResourceDimension;
    switch (pUnorderedAccessView->ResourceDimension)
    {
        case D3D10DDIRESOURCE_BUFFER:
            pUnorderedAccessView->DimensionDesc.Buffer = pCreateUnorderedAccessView->Buffer;
            break;
        case D3D10DDIRESOURCE_TEXTURE1D:
            pUnorderedAccessView->DimensionDesc.Tex1D = pCreateUnorderedAccessView->Tex1D;
            break;
        case D3D10DDIRESOURCE_TEXTURE2D:
            pUnorderedAccessView->DimensionDesc.Tex2D = pCreateUnorderedAccessView->Tex2D;
            break;
        case D3D10DDIRESOURCE_TEXTURE3D:
            pUnorderedAccessView->DimensionDesc.Tex3D = pCreateUnorderedAccessView->Tex3D;
            break;
        default:
            vboxDXDeviceSetError(pDevice, E_INVALIDARG);
            return;
    }

    vboxDXCreateUnorderedAccessView(pDevice, pUnorderedAccessView);
}

static void APIENTRY ddi11DestroyUnorderedAccessView(
    D3D10DDI_HDEVICE hDevice,
    D3D11DDI_HUNORDEREDACCESSVIEW hUnorderedAccessView
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXUNORDEREDACCESSVIEW pUnorderedAccessView = (PVBOXDXUNORDEREDACCESSVIEW)hUnorderedAccessView.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pUnorderedAccessView 0x%p", pDevice, pUnorderedAccessView));

    vboxDXDestroyUnorderedAccessView(pDevice, pUnorderedAccessView);
}

static void APIENTRY ddi11ClearUnorderedAccessViewUint(
    D3D10DDI_HDEVICE hDevice,
    D3D11DDI_HUNORDEREDACCESSVIEW hUnorderedAccessView,
    const UINT Values[4]
)
{
    DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXUNORDEREDACCESSVIEW pUnorderedAccessView = (PVBOXDXUNORDEREDACCESSVIEW)hUnorderedAccessView.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pUnorderedAccessView 0x%p", pDevice, pUnorderedAccessView));

    vboxDXClearUnorderedAccessViewUint(pDevice, pUnorderedAccessView, Values);
}

static void APIENTRY ddi11ClearUnorderedAccessViewFloat(
    D3D10DDI_HDEVICE hDevice,
    D3D11DDI_HUNORDEREDACCESSVIEW hUnorderedAccessView,
    const FLOAT Values[4]
)
{
    DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXUNORDEREDACCESSVIEW pUnorderedAccessView = (PVBOXDXUNORDEREDACCESSVIEW)hUnorderedAccessView.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pUnorderedAccessView 0x%p", pDevice, pUnorderedAccessView));

    vboxDXClearUnorderedAccessViewFloat(pDevice, pUnorderedAccessView, Values);
}

static void APIENTRY ddi11CsSetUnorderedAccessViews(
    D3D10DDI_HDEVICE hDevice,
    UINT StartSlot,
    UINT NumViews,
    const D3D11DDI_HUNORDEREDACCESSVIEW* phUnorderedAccessView,
    const UINT* pUAVInitialCounts
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, StartSlot = %u, NumViews = %u\n", pDevice, StartSlot, NumViews));

    AssertReturnVoidStmt(   NumViews <= SVGA3D_DX11_1_MAX_UAVIEWS
                         && StartSlot < SVGA3D_DX11_1_MAX_UAVIEWS
                         && NumViews + StartSlot <= SVGA3D_DX11_1_MAX_UAVIEWS,
                         vboxDXDeviceSetError(pDevice, E_INVALIDARG));

    /* Fetch View ids. */
    uint32_t aViewIds[SVGA3D_DX11_1_MAX_UAVIEWS];
    for (unsigned i = 0; i < NumViews; ++i)
    {
        VBOXDXUNORDEREDACCESSVIEW *pView = (PVBOXDXUNORDEREDACCESSVIEW)phUnorderedAccessView[i].pDrvPrivate;
        aViewIds[i] = pView ? pView->uUnorderedAccessViewId : SVGA3D_INVALID_ID;
    }

    vboxDXCsSetUnorderedAccessViews(pDevice, StartSlot, NumViews, aViewIds, pUAVInitialCounts);
}

static void APIENTRY ddi11Dispatch(
    D3D10DDI_HDEVICE hDevice,
    UINT ThreadGroupCountX,
    UINT ThreadGroupCountY,
    UINT ThreadGroupCountZ
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice = %p, ThreadGroupCountX %u, ThreadGroupCountY %u, ThreadGroupCountZ %u\n", pDevice, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ));

    vboxDXDispatch(pDevice, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
}

static void APIENTRY ddi11DispatchIndirect(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hBufferForArgs,
    UINT AlignedByteOffsetForArgs
)
{
    DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)hBufferForArgs.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pResource %p, AlignedByteOffsetForArgs %u", pDevice, pResource, AlignedByteOffsetForArgs));

    vboxDXDispatchIndirect(pDevice, pResource, AlignedByteOffsetForArgs);
}

void APIENTRY vboxDXSetResourceMinLOD(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource,
    FLOAT MinLOD
)
{
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(hDevice, hResource, MinLOD);
    LogFlowFuncEnter();
}

static void APIENTRY ddi11CopyStructureCount(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDstBuffer,
    UINT DstAlignedByteOffset,
    D3D11DDI_HUNORDEREDACCESSVIEW hSrcView
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pDstBuffer = (PVBOXDX_RESOURCE)hDstBuffer.pDrvPrivate;
    PVBOXDXUNORDEREDACCESSVIEW pSrcView = (PVBOXDXUNORDEREDACCESSVIEW)hSrcView.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pDstBuffer 0x%p, pSrcView %p, DstAlignedByteOffset %d", pDevice, pDstBuffer, pSrcView, DstAlignedByteOffset));

    vboxDXCopyStructureCount(pDevice, pDstBuffer, DstAlignedByteOffset, pSrcView);
}

static void APIENTRY ddi11_1Discard(
    D3D10DDI_HDEVICE hDevice,
    D3D11DDI_HANDLETYPE HandleType,
    VOID* hResourceOrView,
    const D3D10_DDI_RECT* pRects,
    UINT NumRects
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice %p, HandleType %u, hResourceOrView %p, NumRect %u\n", pDevice, HandleType, hResourceOrView, NumRects));

    RT_NOREF(pDevice, HandleType, hResourceOrView, pRects, NumRects);
    /** @todo "Discards (evicts) an allocation from video display memory" */
}

static void APIENTRY ddi11_1AssignDebugBinary(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADER hShader,
    UINT uBinarySize,
    const VOID* pBinary
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXSHADER pShader = (PVBOXDXSHADER)hShader.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pShader 0x%p, uBinarySize %u", pDevice, pShader, uBinarySize));
    RT_NOREF(pDevice, pShader, uBinarySize, pBinary);
    /* Not used by this driver. */
}

static void APIENTRY ddi10DynamicConstantBufferMapNoOverwrite(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource,
    UINT Subresource,
    D3D10_DDI_MAP DDIMap,
    UINT Flags,
    D3D10DDI_MAPPED_SUBRESOURCE* pMappedSubResource
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)hResource.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pResource 0x%p, subres %d, map %d, flags 0x%X", pDevice, pResource, Subresource, DDIMap, Flags));

    vboxDXResourceMap(pDevice, pResource, Subresource, DDIMap, Flags, pMappedSubResource);
}

static void APIENTRY ddi11_1CheckDirectFlipSupport(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource,
    D3D10DDI_HRESOURCE hResourceDWM,
    UINT CheckDirectFlipFlags,
    BOOL* pSupported
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)hResource.pDrvPrivate;
    PVBOXDX_RESOURCE pResourceDWM = (PVBOXDX_RESOURCE)hResourceDWM.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, pResource 0x%p, pResourceDWM 0x%p, CheckDirectFlipFlags 0x%X", pDevice, pResource, pResourceDWM, CheckDirectFlipFlags));
    RT_NOREF(pDevice, pResource, pResourceDWM, CheckDirectFlipFlags);

    if (pSupported)
        pSupported = FALSE; /* Not supported. Maybe later. */
}

static void APIENTRY ddi11_1ClearView(
    D3D10DDI_HDEVICE hDevice,
    D3D11DDI_HANDLETYPE ViewType,
    VOID* hView,
    const FLOAT Color[4],
    const D3D10_DDI_RECT* pRect,
    UINT NumRects
)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, ViewType %d, pView %p, pRect %p, NumRects %u", pDevice, ViewType, hView, pRect, NumRects));

    if (pDevice->pAdapter->fVBoxCaps & VBSVGA3D_CAP_VIDEO)
    {
        uint32_t ViewId = SVGA3D_INVALID_ID;

        /* "Possible types are the following.
         * D3D10DDI_HT_RENDERTARGETVIEW
         * D3D11DDI_HT_UNORDEREDACCESSVIEW
         * Any D3D11_1DDI_HT_VIDEOXXX type"
         */
        switch (ViewType)
        {
            case D3D10DDI_HT_RENDERTARGETVIEW:
            {
                PVBOXDXRENDERTARGETVIEW pRenderTargetView = (PVBOXDXRENDERTARGETVIEW)hView;
                ViewId = pRenderTargetView->uRenderTargetViewId;
                break;
            }
            case D3D11DDI_HT_UNORDEREDACCESSVIEW:
            {
                PVBOXDXUNORDEREDACCESSVIEW pUnorderedAccessView = (PVBOXDXUNORDEREDACCESSVIEW)hView;
                ViewId = pUnorderedAccessView->uUnorderedAccessViewId;
                break;
            }
            case D3D11_1DDI_HT_VIDEODECODEROUTPUTVIEW:
            {
                PVBOXDXVIDEODECODEROUTPUTVIEW pVideoDecoderOutputView = (PVBOXDXVIDEODECODEROUTPUTVIEW)hView;
                ViewId = pVideoDecoderOutputView->uVideoDecoderOutputViewId;
                break;
            }
            case D3D11_1DDI_HT_VIDEOPROCESSORINPUTVIEW:
            {
                PVBOXDXVIDEOPROCESSORINPUTVIEW pVideoProcessorInputView = (PVBOXDXVIDEOPROCESSORINPUTVIEW)hView;
                ViewId = pVideoProcessorInputView->uVideoProcessorInputViewId;
                break;
            }
            case D3D11_1DDI_HT_VIDEOPROCESSOROUTPUTVIEW:
            {
                PVBOXDXVIDEOPROCESSOROUTPUTVIEW pVideoProcessorOutputView = (PVBOXDXVIDEOPROCESSOROUTPUTVIEW)hView;
                ViewId = pVideoProcessorOutputView->uVideoProcessorOutputViewId;
                break;
            }
            default:
            {
                DEBUG_BREAKPOINT_TEST();
                break;
            }
        }
        if (ViewId != SVGA3D_INVALID_ID)
            vboxDXClearView(pDevice, ViewType, ViewId, Color, pRect, NumRects);
        return;
    }

    if (ViewType == D3D10DDI_HT_RENDERTARGETVIEW)
    {
        PVBOXDXRENDERTARGETVIEW pRenderTargetView = (PVBOXDXRENDERTARGETVIEW)hView;
        if (pRect == NULL)
            vboxDXClearRenderTargetView(pDevice, pRenderTargetView, Color);
        else
            vboxDXClearRenderTargetViewRegion(pDevice, pRenderTargetView, Color, pRect, NumRects);
    }
    else
        DEBUG_BREAKPOINT_TEST();
}

static HRESULT APIENTRY dxgiPresent(DXGI_DDI_ARG_PRESENT *pPresentArg)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)pPresentArg->hDevice;
    PVBOXDX_RESOURCE pSrcResource = (PVBOXDX_RESOURCE)pPresentArg->hSurfaceToPresent;
    PVBOXDX_RESOURCE pDstResource = (PVBOXDX_RESOURCE)pPresentArg->hDstResource;
    LogFlowFunc(("pDevice 0x%p, pSrcResource 0x%p[%d], pDstResource 0x%p[%d], pDXGIContext %p, Flags 0x%08X, FlipInterval %d",
                 pDevice, pSrcResource, pPresentArg->SrcSubResourceIndex,
                 pDstResource, pPresentArg->DstSubResourceIndex, pPresentArg->pDXGIContext,
                 pPresentArg->Flags.Value, pPresentArg->FlipInterval));

    HRESULT hr = vboxDXFlush(pDevice, true);
    AssertReturnStmt(SUCCEEDED(hr), vboxDXDeviceSetError(pDevice, hr), hr);

    DXGIDDICB_PRESENT ddiPresent;
    RT_ZERO(ddiPresent);
    ddiPresent.hSrcAllocation     = vboxDXGetAllocation(pSrcResource);
    ddiPresent.hDstAllocation     = vboxDXGetAllocation(pDstResource);
    ddiPresent.pDXGIContext       = pPresentArg->pDXGIContext;
    ddiPresent.hContext           = pDevice->hContext;

    hr = pDevice->pDXGIBaseCallbacks->pfnPresentCb(pDevice->hRTDevice.handle, &ddiPresent);
    AssertReturnStmt(SUCCEEDED(hr), vboxDXDeviceSetError(pDevice, hr), hr);

    return STATUS_SUCCESS;
}

static HRESULT APIENTRY dxgiGetGammaCaps(DXGI_DDI_ARG_GET_GAMMA_CONTROL_CAPS *pGammaArg)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)pGammaArg->hDevice;
    LogFlowFunc(("pDevice 0x%p\n", pDevice));
    RT_NOREF(pDevice);

    pGammaArg->pGammaCapabilities->ScaleAndOffsetSupported = FALSE;
    pGammaArg->pGammaCapabilities->MaxConvertedValue = 0.0f;
    pGammaArg->pGammaCapabilities->MinConvertedValue = 0.0f;
    pGammaArg->pGammaCapabilities->NumGammaControlPoints = 0;
    RT_ZERO(pGammaArg->pGammaCapabilities->ControlPointPositions);

    return S_OK;
}

static HRESULT APIENTRY dxgiSetDisplayMode(DXGI_DDI_ARG_SETDISPLAYMODE *pDisplayModeData)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)pDisplayModeData->hDevice;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)pDisplayModeData->hResource;
    LogFlowFunc(("pDevice 0x%p, pResource 0x%p, subres %d", pDevice, pResource, pDisplayModeData->SubResourceIndex));

    AssertReturn(pResource->AllocationDesc.fPrimary && pDisplayModeData->SubResourceIndex == 0, E_INVALIDARG);

    D3DDDICB_SETDISPLAYMODE ddiSetDisplayMode;
    RT_ZERO(ddiSetDisplayMode);
    ddiSetDisplayMode.hPrimaryAllocation = vboxDXGetAllocation(pResource);
    HRESULT hr = pDevice->pRTCallbacks->pfnSetDisplayModeCb(pDevice->hRTDevice.handle, &ddiSetDisplayMode);
    AssertReturn(SUCCEEDED(hr), hr);

    return STATUS_SUCCESS;
}

HRESULT APIENTRY vboxDXGISetResourcePriority(DXGI_DDI_ARG_SETRESOURCEPRIORITY *)
{
    DEBUG_BREAKPOINT_TEST();
    LogFlowFuncEnter();
    return S_OK;
}

static HRESULT APIENTRY dxgiQueryResourceResidency(DXGI_DDI_ARG_QUERYRESOURCERESIDENCY *pQueryResourceResidency)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)pQueryResourceResidency->hDevice;
    PVBOXDX_RESOURCE *papResources = (PVBOXDX_RESOURCE *)pQueryResourceResidency->pResources;
    LogFlowFunc(("pDevice 0x%p, Resources %d", pDevice, pQueryResourceResidency->Resources));

    /* "If pfnQueryResidencyCb returns D3DDDI_RESIDENCYSTATUS_NOTRESIDENT for any query,
     *  QueryResourceResidencyDXGI must return S_NOT_RESIDENT."
     */
    bool fNotResident = false;

    /* "If pfnQueryResidencyCb returns D3DDDI_RESIDENCYSTATUS_RESIDENTINSHAREDMEMORY for any query
     *  and does not return D3DDDI_RESIDENCYSTATUS_NOTRESIDENT for any query, QueryResourceResidencyDXGI
     *  must return S_RESIDENT_IN_SHARED_MEMORY."
     */
    bool fResidentInSharedMemory = false;

    /* "QueryResourceResidencyDXGI must return S_OK only if all calls to pfnQueryResidencyCb for all
     * queries return D3DDDI_RESIDENCYSTATUS_RESIDENTINGPUMEMORY."
     */

    for (SIZE_T i = 0; i < pQueryResourceResidency->Resources; ++i)
    {
        D3DDDI_RESIDENCYSTATUS ResidencyStatus = (D3DDDI_RESIDENCYSTATUS)0;

        D3DKMT_HANDLE const hAllocation = vboxDXGetAllocation(papResources[i]);

        D3DDDICB_QUERYRESIDENCY ddiQueryResidency;
        RT_ZERO(ddiQueryResidency);
        ddiQueryResidency.NumAllocations   = 1;
        ddiQueryResidency.HandleList       = &hAllocation;
        ddiQueryResidency.pResidencyStatus = &ResidencyStatus;

        HRESULT hr = pDevice->pRTCallbacks->pfnQueryResidencyCb(pDevice->hRTDevice.handle, &ddiQueryResidency);
        AssertReturn(SUCCEEDED(hr), hr);

        if (ResidencyStatus == D3DDDI_RESIDENCYSTATUS_RESIDENTINGPUMEMORY)
        {
            pQueryResourceResidency->pStatus[i] = DXGI_DDI_RESIDENCY_FULLY_RESIDENT;
        }
        else if (ResidencyStatus == D3DDDI_RESIDENCYSTATUS_RESIDENTINSHAREDMEMORY)
        {
            fResidentInSharedMemory = true;
            pQueryResourceResidency->pStatus[i] = DXGI_DDI_RESIDENCY_RESIDENT_IN_SHARED_MEMORY;
        }
        else if (ResidencyStatus == D3DDDI_RESIDENCYSTATUS_NOTRESIDENT)
        {
            fNotResident = true;
            pQueryResourceResidency->pStatus[i] = DXGI_DDI_RESIDENCY_EVICTED_TO_DISK;
        }
        else
            AssertFailedReturn(E_FAIL);
    }

    if (fNotResident)
        return S_NOT_RESIDENT;

    if (fResidentInSharedMemory)
        return S_RESIDENT_IN_SHARED_MEMORY;

    return S_OK;
}

static HRESULT APIENTRY dxgiRotateResourceIdentities(DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES *pRotateResourceIdentities)
{
    // DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)pRotateResourceIdentities->hDevice;
    LogFlowFunc(("pDevice 0x%p, Resources %d", pDevice, pRotateResourceIdentities->Resources));

    if (pRotateResourceIdentities->Resources <= 1)
        return S_OK;

    return vboxDXRotateResourceIdentities(pDevice, pRotateResourceIdentities->Resources, (PVBOXDX_RESOURCE *)pRotateResourceIdentities->pResources);
}

static HRESULT APIENTRY dxgiBlt(DXGI_DDI_ARG_BLT *pBlt)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)pBlt->hDevice;
    PVBOXDX_RESOURCE pDstResource = (PVBOXDX_RESOURCE)pBlt->hDstResource;
    PVBOXDX_RESOURCE pSrcResource = (PVBOXDX_RESOURCE)pBlt->hSrcResource;
    LogFlowFunc(("pDevice 0x%p, pDstResource 0x%p[%u], pSrcResource 0x%p[%u], %d,%d %d,%d, flags 0x%x, rotate %u",
                 pDevice, pDstResource, pBlt->DstSubresource, pSrcResource, pBlt->SrcSubresource,
                 pBlt->DstLeft, pBlt->DstTop, pBlt->DstRight, pBlt->DstBottom, pBlt->Flags, pBlt->Rotate));

    return vboxDXBlt(pDevice, pDstResource, pBlt->DstSubresource, pSrcResource, pBlt->SrcSubresource,
                     pBlt->DstLeft, pBlt->DstTop, pBlt->DstRight, pBlt->DstBottom, pBlt->Flags, pBlt->Rotate);
}

static HRESULT APIENTRY dxgiResolveSharedResource(DXGI_DDI_ARG_RESOLVESHAREDRESOURCE *pResolveSharedResource)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)pResolveSharedResource->hDevice;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)pResolveSharedResource->hResource;
    LogFlowFunc(("pDevice 0x%p, pResource 0x%p", pDevice, pResource));

    vboxDXFlush(pDevice, true);

    RT_NOREF(pResource);

    return S_OK;
}

HRESULT APIENTRY vboxDXGIBlt1(DXGI_DDI_ARG_BLT1 *)
{
    DEBUG_BREAKPOINT_TEST();
    LogFlowFuncEnter();
    return S_OK;
}

static HRESULT APIENTRY dxgiOfferResources(DXGI_DDI_ARG_OFFERRESOURCES *pOfferResources)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)pOfferResources->hDevice;
    LogFlowFunc(("pDevice 0x%p, Resources %d, Priority %d", pDevice, pOfferResources->Resources, pOfferResources->Priority));

#ifdef LOG_ENABLED
    for (unsigned i = 0; i < pOfferResources->Resources; ++i)
    {
        PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)pOfferResources->pResources[i];
        LogFlowFunc(("Resources[%d]: pResource %p, hAllocation 0x%08x", i, pResource, vboxDXGetAllocation(pResource)));
    }
#endif

    return vboxDXOfferResources(pDevice, pOfferResources->Resources, (PVBOXDX_RESOURCE *)pOfferResources->pResources, pOfferResources->Priority);
}

static HRESULT APIENTRY dxgiReclaimResources(DXGI_DDI_ARG_RECLAIMRESOURCES *pReclaimResources)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)pReclaimResources->hDevice;
    LogFlowFunc(("pDevice 0x%p, Resources %d", pDevice, pReclaimResources->Resources));

#ifdef LOG_ENABLED
    for (unsigned i = 0; i < pReclaimResources->Resources; ++i)
    {
        PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)pReclaimResources->pResources[i];
        LogFlowFunc(("Resources[%d]: pResource %p, hAllocation 0x%08x, Discarded %d",
                      i, pResource, vboxDXGetAllocation(pResource), pReclaimResources->pDiscarded ? pReclaimResources->pDiscarded[i] : 0));
    }
#endif

    return vboxDXReclaimResources(pDevice, pReclaimResources->Resources, (PVBOXDX_RESOURCE *)pReclaimResources->pResources, pReclaimResources->pDiscarded);
}


static void APIENTRY ddi10DestroyDevice(D3D10DDI_HDEVICE hDevice)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p", pDevice));

    vboxDXDestroyDevice(pDevice);
}


static HRESULT APIENTRY ddi10RetrieveSubObject(
    D3D10DDI_HDEVICE hDevice,
    UINT32 SubDeviceID,
    SIZE_T ParamSize,
    void *pParams,
    SIZE_T OutputParamSize,
    void *pOutputParamsBuffer
)
{
    RT_NOREF(OutputParamSize, pOutputParamsBuffer);

    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    LogFlowFunc(("pDevice 0x%p, SubDeviceID %u, ParamSize %u, OutputParamSize %u", pDevice, SubDeviceID, ParamSize, OutputParamSize));

    if (SubDeviceID == D3D11_1DDI_VIDEO_FUNCTIONS)
    {
        AssertReturn(ParamSize >= sizeof(D3D11_1DDI_VIDEO_INPUT), E_INVALIDARG);

        if (pDevice->pAdapter->fVBoxCaps & VBSVGA3D_CAP_VIDEO)
        {
            D3D11_1DDI_VIDEO_INPUT *pVideoInput = (D3D11_1DDI_VIDEO_INPUT *)pParams;
            return ddi11_1RetrieveVideoFunctions(pDevice, pVideoInput);
        }
        return E_FAIL;
    }

    DEBUG_BREAKPOINT_TEST();
    return E_FAIL;
}


/*
 * Adapter functions.
 */

static SIZE_T APIENTRY vboxDXCalcPrivateDeviceSize(D3D10DDI_HADAPTER hAdapter, const D3D10DDIARG_CALCPRIVATEDEVICESIZE* pData)
{
    RT_NOREF(hAdapter, pData);
    LogFlow(("vboxDXCalcPrivateDeviceSize: Interface 0x%08x, Version 0x%08x, Flags 0x%08x", pData->Interface, pData->Version, pData->Flags));

    return sizeof(VBOXDX_DEVICE);
}

static HRESULT APIENTRY vboxDXCreateDevice(D3D10DDI_HADAPTER hAdapter, D3D10DDIARG_CREATEDEVICE *pCreateData)
{
    //DEBUG_BREAKPOINT_TEST();
    LogFlowFunc(("Interface 0x%08x, Version 0x%08x, PipelineLevel %d",
                 pCreateData->Interface, pCreateData->Version, D3D11DDI_EXTRACT_3DPIPELINELEVEL_FROM_FLAGS(pCreateData->Flags)));

    PVBOXDXADAPTER pAdapter = (PVBOXDXADAPTER)hAdapter.pDrvPrivate;
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)pCreateData->hDrvDevice.pDrvPrivate;
    RT_ZERO(*pDevice);

    /* Verify that the requested device level is supported. */
    AssertReturn(isInterfaceSupported(pCreateData->Interface), E_FAIL);

    /* Remember which adapter has created this device. */
    pDevice->pAdapter = pAdapter;

    /* Fetch the supplied Direct3D runtime data. */
    pDevice->hRTDevice          = pCreateData->hRTDevice;
    pDevice->uDDIVersion        = pCreateData->Interface;
    pDevice->uCreateDeviceFlags = pCreateData->Flags;
    pDevice->pRTCallbacks       = pCreateData->pKTCallbacks;
    pDevice->pDXGIBaseCallbacks = pCreateData->DXGIBaseDDI.pDXGIBaseCallbacks;
    pDevice->hRTCoreLayer       = pCreateData->hRTCoreLayer;
    pDevice->pUMCallbacks       = pCreateData->p11UMCallbacks;

    /* Create the kernel mode context for this device. */
    HRESULT hr = vboxDXDeviceInit(pDevice);
    AssertReturn(SUCCEEDED(hr), hr);

    /* Success. Fill the return data for the Direct3D runtime. */

    if (pCreateData->Interface == D3D11_1_DDI_INTERFACE_VERSION)
    {
        /*
         * 11.1
         */
        D3D11_1DDI_DEVICEFUNCS *p11_1DeviceFuncs = pCreateData->p11_1DeviceFuncs;

        /* Order of functions is in decreasing order of priority ( as far as performance is concerned ). */
        /* High frequency functions. */
        p11_1DeviceFuncs->pfnDefaultConstantBufferUpdateSubresourceUP  = ddi11_1DefaultConstantBufferUpdateSubresourceUP;
        p11_1DeviceFuncs->pfnVsSetConstantBuffers                      = ddi11_1VsSetConstantBuffers;
        p11_1DeviceFuncs->pfnPsSetShaderResources                      = ddi10PsSetShaderResources;
        p11_1DeviceFuncs->pfnPsSetShader                               = ddi10PsSetShader;
        p11_1DeviceFuncs->pfnPsSetSamplers                             = ddi10PsSetSamplers;
        p11_1DeviceFuncs->pfnVsSetShader                               = ddi10VsSetShader;
        p11_1DeviceFuncs->pfnDrawIndexed                               = ddi10DrawIndexed;
        p11_1DeviceFuncs->pfnDraw                                      = ddi10Draw;
        p11_1DeviceFuncs->pfnDynamicIABufferMapNoOverwrite             = ddi10DynamicIABufferMapNoOverwrite;
        p11_1DeviceFuncs->pfnDynamicIABufferUnmap                      = ddi10DynamicIABufferUnmap;
        p11_1DeviceFuncs->pfnDynamicConstantBufferMapDiscard           = ddi10DynamicConstantBufferMapDiscard;
        p11_1DeviceFuncs->pfnDynamicIABufferMapDiscard                 = ddi10DynamicIABufferMapDiscard;
        p11_1DeviceFuncs->pfnDynamicConstantBufferUnmap                = ddi10DynamicConstantBufferUnmap;
        p11_1DeviceFuncs->pfnPsSetConstantBuffers                      = ddi11_1PsSetConstantBuffers;
        p11_1DeviceFuncs->pfnIaSetInputLayout                          = ddi10IaSetInputLayout;
        p11_1DeviceFuncs->pfnIaSetVertexBuffers                        = ddi10IaSetVertexBuffers;
        p11_1DeviceFuncs->pfnIaSetIndexBuffer                          = ddi10IaSetIndexBuffer;

        /* Middle frequency functions. */
        p11_1DeviceFuncs->pfnDrawIndexedInstanced                      = ddi10DrawIndexedInstanced;
        p11_1DeviceFuncs->pfnDrawInstanced                             = ddi10DrawInstanced;
        p11_1DeviceFuncs->pfnDynamicResourceMapDiscard                 = ddi10DynamicResourceMapDiscard;
        p11_1DeviceFuncs->pfnDynamicResourceUnmap                      = ddi10DynamicResourceUnmap;
        p11_1DeviceFuncs->pfnGsSetConstantBuffers                      = ddi11_1GsSetConstantBuffers;
        p11_1DeviceFuncs->pfnGsSetShader                               = ddi10GsSetShader;
        p11_1DeviceFuncs->pfnIaSetTopology                             = ddi10IaSetTopology;
        p11_1DeviceFuncs->pfnStagingResourceMap                        = ddi10StagingResourceMap;
        p11_1DeviceFuncs->pfnStagingResourceUnmap                      = ddi10StagingResourceUnmap;
        p11_1DeviceFuncs->pfnVsSetShaderResources                      = ddi10VsSetShaderResources;
        p11_1DeviceFuncs->pfnVsSetSamplers                             = ddi10VsSetSamplers;
        p11_1DeviceFuncs->pfnGsSetShaderResources                      = ddi10GsSetShaderResources;
        p11_1DeviceFuncs->pfnGsSetSamplers                             = ddi10GsSetSamplers;
        p11_1DeviceFuncs->pfnSetRenderTargets                          = ddi11SetRenderTargets;
        p11_1DeviceFuncs->pfnShaderResourceViewReadAfterWriteHazard    = ddi10ShaderResourceViewReadAfterWriteHazard;
        p11_1DeviceFuncs->pfnResourceReadAfterWriteHazard              = ddi10ResourceReadAfterWriteHazard;
        p11_1DeviceFuncs->pfnSetBlendState                             = ddi10SetBlendState;
        p11_1DeviceFuncs->pfnSetDepthStencilState                      = ddi10SetDepthStencilState;
        p11_1DeviceFuncs->pfnSetRasterizerState                        = ddi10SetRasterizerState;
        p11_1DeviceFuncs->pfnQueryEnd                                  = ddi10QueryEnd;
        p11_1DeviceFuncs->pfnQueryBegin                                = ddi10QueryBegin;
        p11_1DeviceFuncs->pfnResourceCopyRegion                        = ddi11_1ResourceCopyRegion;
        p11_1DeviceFuncs->pfnResourceUpdateSubresourceUP               = ddi11_1ResourceUpdateSubresourceUP;
        p11_1DeviceFuncs->pfnSoSetTargets                              = ddi10SoSetTargets;
        p11_1DeviceFuncs->pfnDrawAuto                                  = ddi10DrawAuto;
        p11_1DeviceFuncs->pfnSetViewports                              = ddi10SetViewports;
        p11_1DeviceFuncs->pfnSetScissorRects                           = ddi10SetScissorRects;
        p11_1DeviceFuncs->pfnClearRenderTargetView                     = ddi10ClearRenderTargetView;
        p11_1DeviceFuncs->pfnClearDepthStencilView                     = ddi10ClearDepthStencilView;
        p11_1DeviceFuncs->pfnSetPredication                            = ddi10SetPredication;
        p11_1DeviceFuncs->pfnQueryGetData                              = ddi10QueryGetData;
        p11_1DeviceFuncs->pfnFlush                                     = ddi11_1Flush;
        p11_1DeviceFuncs->pfnGenMips                                   = ddi10GenMips;
        p11_1DeviceFuncs->pfnResourceCopy                              = ddi10ResourceCopy;
        p11_1DeviceFuncs->pfnResourceResolveSubresource                = ddi10ResourceResolveSubresource;

        /* Infrequent paths. */
        p11_1DeviceFuncs->pfnResourceMap                               = ddi10ResourceMap;
        p11_1DeviceFuncs->pfnResourceUnmap                             = ddi10ResourceUnmap;
        p11_1DeviceFuncs->pfnResourceIsStagingBusy                     = vboxDXResourceIsStagingBusy;
        p11_1DeviceFuncs->pfnRelocateDeviceFuncs                       = ddi11_1RelocateDeviceFuncs;
        p11_1DeviceFuncs->pfnCalcPrivateResourceSize                   = ddi11CalcPrivateResourceSize;
        p11_1DeviceFuncs->pfnCalcPrivateOpenedResourceSize             = ddi10CalcPrivateOpenedResourceSize;
        p11_1DeviceFuncs->pfnCreateResource                            = ddi11CreateResource;
        p11_1DeviceFuncs->pfnOpenResource                              = ddi10OpenResource;
        p11_1DeviceFuncs->pfnDestroyResource                           = ddi10DestroyResource;
        p11_1DeviceFuncs->pfnCalcPrivateShaderResourceViewSize         = ddi11CalcPrivateShaderResourceViewSize;
        p11_1DeviceFuncs->pfnCreateShaderResourceView                  = ddi11CreateShaderResourceView;
        p11_1DeviceFuncs->pfnDestroyShaderResourceView                 = ddi10DestroyShaderResourceView;
        p11_1DeviceFuncs->pfnCalcPrivateRenderTargetViewSize           = ddi10CalcPrivateRenderTargetViewSize;
        p11_1DeviceFuncs->pfnCreateRenderTargetView                    = ddi10CreateRenderTargetView;
        p11_1DeviceFuncs->pfnDestroyRenderTargetView                   = ddi10DestroyRenderTargetView;
        p11_1DeviceFuncs->pfnCalcPrivateDepthStencilViewSize           = ddi11CalcPrivateDepthStencilViewSize;
        p11_1DeviceFuncs->pfnCreateDepthStencilView                    = ddi11CreateDepthStencilView;
        p11_1DeviceFuncs->pfnDestroyDepthStencilView                   = ddi10DestroyDepthStencilView;
        p11_1DeviceFuncs->pfnCalcPrivateElementLayoutSize              = ddi10CalcPrivateElementLayoutSize;
        p11_1DeviceFuncs->pfnCreateElementLayout                       = ddi10CreateElementLayout;
        p11_1DeviceFuncs->pfnDestroyElementLayout                      = ddi10DestroyElementLayout;
        p11_1DeviceFuncs->pfnCalcPrivateBlendStateSize                 = ddi11_1CalcPrivateBlendStateSize;
        p11_1DeviceFuncs->pfnCreateBlendState                          = ddi11_1CreateBlendState;
        p11_1DeviceFuncs->pfnDestroyBlendState                         = ddi10DestroyBlendState;
        p11_1DeviceFuncs->pfnCalcPrivateDepthStencilStateSize          = ddi10CalcPrivateDepthStencilStateSize;
        p11_1DeviceFuncs->pfnCreateDepthStencilState                   = ddi10CreateDepthStencilState;
        p11_1DeviceFuncs->pfnDestroyDepthStencilState                  = ddi10DestroyDepthStencilState;
        p11_1DeviceFuncs->pfnCalcPrivateRasterizerStateSize            = ddi11_1CalcPrivateRasterizerStateSize;
        p11_1DeviceFuncs->pfnCreateRasterizerState                     = ddi11_1CreateRasterizerState;
        p11_1DeviceFuncs->pfnDestroyRasterizerState                    = ddi10DestroyRasterizerState;
        p11_1DeviceFuncs->pfnCalcPrivateShaderSize                     = ddi11_1CalcPrivateShaderSize;
        p11_1DeviceFuncs->pfnCreateVertexShader                        = ddi11_1CreateVertexShader;
        p11_1DeviceFuncs->pfnCreateGeometryShader                      = ddi11_1CreateGeometryShader;
        p11_1DeviceFuncs->pfnCreatePixelShader                         = ddi11_1CreatePixelShader;
        p11_1DeviceFuncs->pfnCalcPrivateGeometryShaderWithStreamOutput = ddi11_1CalcPrivateGeometryShaderWithStreamOutput;
        p11_1DeviceFuncs->pfnCreateGeometryShaderWithStreamOutput      = ddi11_1CreateGeometryShaderWithStreamOutput;
        p11_1DeviceFuncs->pfnDestroyShader                             = ddi10DestroyShader;
        p11_1DeviceFuncs->pfnCalcPrivateSamplerSize                    = ddi10CalcPrivateSamplerSize;
        p11_1DeviceFuncs->pfnCreateSampler                             = ddi10CreateSampler;
        p11_1DeviceFuncs->pfnDestroySampler                            = ddi10DestroySampler;
        p11_1DeviceFuncs->pfnCalcPrivateQuerySize                      = ddi10CalcPrivateQuerySize;
        p11_1DeviceFuncs->pfnCreateQuery                               = ddi10CreateQuery;
        p11_1DeviceFuncs->pfnDestroyQuery                              = ddi10DestroyQuery;

        p11_1DeviceFuncs->pfnCheckFormatSupport                        = vboxDXCheckFormatSupport;
        p11_1DeviceFuncs->pfnCheckMultisampleQualityLevels             = vboxDXCheckMultisampleQualityLevels;
        p11_1DeviceFuncs->pfnCheckCounterInfo                          = ddi10CheckCounterInfo;
        p11_1DeviceFuncs->pfnCheckCounter                              = ddi10CheckCounter;

        p11_1DeviceFuncs->pfnDestroyDevice                             = ddi10DestroyDevice;
        p11_1DeviceFuncs->pfnSetTextFilterSize                         = ddi10SetTextFilterSize;

        /* Additional 10.1 entries */
        p11_1DeviceFuncs->pfnResourceConvert                           = vboxDXResourceConvert;
        p11_1DeviceFuncs->pfnResourceConvertRegion                     = vboxDXResourceConvertRegion;

        /* Additional 11.0 entries */
        p11_1DeviceFuncs->pfnDrawIndexedInstancedIndirect              = ddi11DrawIndexedInstancedIndirect;
        p11_1DeviceFuncs->pfnDrawInstancedIndirect                     = ddi11DrawInstancedIndirect;
        p11_1DeviceFuncs->pfnCommandListExecute                        = 0;
        p11_1DeviceFuncs->pfnHsSetShaderResources                      = ddi10HsSetShaderResources;
        p11_1DeviceFuncs->pfnHsSetShader                               = ddi10HsSetShader;
        p11_1DeviceFuncs->pfnHsSetSamplers                             = ddi10HsSetSamplers;
        p11_1DeviceFuncs->pfnHsSetConstantBuffers                      = ddi11_1HsSetConstantBuffers;
        p11_1DeviceFuncs->pfnDsSetShaderResources                      = ddi10DsSetShaderResources;
        p11_1DeviceFuncs->pfnDsSetShader                               = ddi10DsSetShader;
        p11_1DeviceFuncs->pfnDsSetSamplers                             = ddi10DsSetSamplers;
        p11_1DeviceFuncs->pfnDsSetConstantBuffers                      = ddi11_1DsSetConstantBuffers;
        p11_1DeviceFuncs->pfnCreateHullShader                          = ddi11_1CreateHullShader;
        p11_1DeviceFuncs->pfnCreateDomainShader                        = ddi11_1CreateDomainShader;
        p11_1DeviceFuncs->pfnCheckDeferredContextHandleSizes           = 0;
        p11_1DeviceFuncs->pfnCalcDeferredContextHandleSize             = 0;
        p11_1DeviceFuncs->pfnCalcPrivateDeferredContextSize            = 0;
        p11_1DeviceFuncs->pfnCreateDeferredContext                     = 0;
        p11_1DeviceFuncs->pfnAbandonCommandList                        = 0;
        p11_1DeviceFuncs->pfnCalcPrivateCommandListSize                = 0;
        p11_1DeviceFuncs->pfnCreateCommandList                         = 0;
        p11_1DeviceFuncs->pfnDestroyCommandList                        = 0;
        p11_1DeviceFuncs->pfnCalcPrivateTessellationShaderSize         = ddi11_1CalcPrivateTessellationShaderSize;
        p11_1DeviceFuncs->pfnPsSetShaderWithIfaces                     = vboxDXPsSetShaderWithIfaces;
        p11_1DeviceFuncs->pfnVsSetShaderWithIfaces                     = vboxDXVsSetShaderWithIfaces;
        p11_1DeviceFuncs->pfnGsSetShaderWithIfaces                     = vboxDXGsSetShaderWithIfaces;
        p11_1DeviceFuncs->pfnHsSetShaderWithIfaces                     = vboxDXHsSetShaderWithIfaces;
        p11_1DeviceFuncs->pfnDsSetShaderWithIfaces                     = vboxDXDsSetShaderWithIfaces;
        p11_1DeviceFuncs->pfnCsSetShaderWithIfaces                     = vboxDXCsSetShaderWithIfaces;
        p11_1DeviceFuncs->pfnCreateComputeShader                       = ddi11CreateComputeShader;
        p11_1DeviceFuncs->pfnCsSetShader                               = ddi10CsSetShader;
        p11_1DeviceFuncs->pfnCsSetShaderResources                      = ddi10CsSetShaderResources;
        p11_1DeviceFuncs->pfnCsSetSamplers                             = ddi10CsSetSamplers;
        p11_1DeviceFuncs->pfnCsSetConstantBuffers                      = ddi11_1CsSetConstantBuffers;
        p11_1DeviceFuncs->pfnCalcPrivateUnorderedAccessViewSize        = ddi11CalcPrivateUnorderedAccessViewSize;
        p11_1DeviceFuncs->pfnCreateUnorderedAccessView                 = ddi11CreateUnorderedAccessView;
        p11_1DeviceFuncs->pfnDestroyUnorderedAccessView                = ddi11DestroyUnorderedAccessView;
        p11_1DeviceFuncs->pfnClearUnorderedAccessViewUint              = ddi11ClearUnorderedAccessViewUint;
        p11_1DeviceFuncs->pfnClearUnorderedAccessViewFloat             = ddi11ClearUnorderedAccessViewFloat;
        p11_1DeviceFuncs->pfnCsSetUnorderedAccessViews                 = ddi11CsSetUnorderedAccessViews;
        p11_1DeviceFuncs->pfnDispatch                                  = ddi11Dispatch;
        p11_1DeviceFuncs->pfnDispatchIndirect                          = ddi11DispatchIndirect;
        p11_1DeviceFuncs->pfnSetResourceMinLOD                         = vboxDXSetResourceMinLOD;
        p11_1DeviceFuncs->pfnCopyStructureCount                        = ddi11CopyStructureCount;
        p11_1DeviceFuncs->pfnRecycleCommandList                        = 0;
        p11_1DeviceFuncs->pfnRecycleCreateCommandList                  = 0;
        p11_1DeviceFuncs->pfnRecycleCreateDeferredContext              = 0;
        p11_1DeviceFuncs->pfnRecycleDestroyCommandList                 = 0;

        /* Additional 11.1 entries */
        p11_1DeviceFuncs->pfnDiscard                                   = ddi11_1Discard;
        p11_1DeviceFuncs->pfnAssignDebugBinary                         = ddi11_1AssignDebugBinary;
        p11_1DeviceFuncs->pfnDynamicConstantBufferMapNoOverwrite       = ddi10DynamicConstantBufferMapNoOverwrite;
        p11_1DeviceFuncs->pfnCheckDirectFlipSupport                    = ddi11_1CheckDirectFlipSupport;
        p11_1DeviceFuncs->pfnClearView                                 = ddi11_1ClearView;
    }
    else if (pCreateData->Interface == D3D11_0_DDI_INTERFACE_VERSION)
    {
        /*
         * 11.0
         */
        D3D11DDI_DEVICEFUNCS *p11DeviceFuncs = pCreateData->p11DeviceFuncs;

        /* Order of functions is in decreasing order of priority ( as far as performance is concerned ). */
        /* High frequency functions. */
        p11DeviceFuncs->pfnDefaultConstantBufferUpdateSubresourceUP  = ddi10DefaultConstantBufferUpdateSubresourceUP;
        p11DeviceFuncs->pfnVsSetConstantBuffers                      = ddi10VsSetConstantBuffers;
        p11DeviceFuncs->pfnPsSetShaderResources                      = ddi10PsSetShaderResources;
        p11DeviceFuncs->pfnPsSetShader                               = ddi10PsSetShader;
        p11DeviceFuncs->pfnPsSetSamplers                             = ddi10PsSetSamplers;
        p11DeviceFuncs->pfnVsSetShader                               = ddi10VsSetShader;
        p11DeviceFuncs->pfnDrawIndexed                               = ddi10DrawIndexed;
        p11DeviceFuncs->pfnDraw                                      = ddi10Draw;
        p11DeviceFuncs->pfnDynamicIABufferMapNoOverwrite             = ddi10DynamicIABufferMapNoOverwrite;
        p11DeviceFuncs->pfnDynamicIABufferUnmap                      = ddi10DynamicIABufferUnmap;
        p11DeviceFuncs->pfnDynamicConstantBufferMapDiscard           = ddi10DynamicConstantBufferMapDiscard;
        p11DeviceFuncs->pfnDynamicIABufferMapDiscard                 = ddi10DynamicIABufferMapDiscard;
        p11DeviceFuncs->pfnDynamicConstantBufferUnmap                = ddi10DynamicConstantBufferUnmap;
        p11DeviceFuncs->pfnPsSetConstantBuffers                      = ddi10PsSetConstantBuffers;
        p11DeviceFuncs->pfnIaSetInputLayout                          = ddi10IaSetInputLayout;
        p11DeviceFuncs->pfnIaSetVertexBuffers                        = ddi10IaSetVertexBuffers;
        p11DeviceFuncs->pfnIaSetIndexBuffer                          = ddi10IaSetIndexBuffer;

        /* Middle frequency functions. */
        p11DeviceFuncs->pfnDrawIndexedInstanced                      = ddi10DrawIndexedInstanced;
        p11DeviceFuncs->pfnDrawInstanced                             = ddi10DrawInstanced;
        p11DeviceFuncs->pfnDynamicResourceMapDiscard                 = ddi10DynamicResourceMapDiscard;
        p11DeviceFuncs->pfnDynamicResourceUnmap                      = ddi10DynamicResourceUnmap;
        p11DeviceFuncs->pfnGsSetConstantBuffers                      = ddi10GsSetConstantBuffers;
        p11DeviceFuncs->pfnGsSetShader                               = ddi10GsSetShader;
        p11DeviceFuncs->pfnIaSetTopology                             = ddi10IaSetTopology;
        p11DeviceFuncs->pfnStagingResourceMap                        = ddi10StagingResourceMap;
        p11DeviceFuncs->pfnStagingResourceUnmap                      = ddi10StagingResourceUnmap;
        p11DeviceFuncs->pfnVsSetShaderResources                      = ddi10VsSetShaderResources;
        p11DeviceFuncs->pfnVsSetSamplers                             = ddi10VsSetSamplers;
        p11DeviceFuncs->pfnGsSetShaderResources                      = ddi10GsSetShaderResources;
        p11DeviceFuncs->pfnGsSetSamplers                             = ddi10GsSetSamplers;
        p11DeviceFuncs->pfnSetRenderTargets                          = ddi11SetRenderTargets;
        p11DeviceFuncs->pfnShaderResourceViewReadAfterWriteHazard    = ddi10ShaderResourceViewReadAfterWriteHazard;
        p11DeviceFuncs->pfnResourceReadAfterWriteHazard              = ddi10ResourceReadAfterWriteHazard;
        p11DeviceFuncs->pfnSetBlendState                             = ddi10SetBlendState;
        p11DeviceFuncs->pfnSetDepthStencilState                      = ddi10SetDepthStencilState;
        p11DeviceFuncs->pfnSetRasterizerState                        = ddi10SetRasterizerState;
        p11DeviceFuncs->pfnQueryEnd                                  = ddi10QueryEnd;
        p11DeviceFuncs->pfnQueryBegin                                = ddi10QueryBegin;
        p11DeviceFuncs->pfnResourceCopyRegion                        = ddi10ResourceCopyRegion;
        p11DeviceFuncs->pfnResourceUpdateSubresourceUP               = ddi10ResourceUpdateSubresourceUP;
        p11DeviceFuncs->pfnSoSetTargets                              = ddi10SoSetTargets;
        p11DeviceFuncs->pfnDrawAuto                                  = ddi10DrawAuto;
        p11DeviceFuncs->pfnSetViewports                              = ddi10SetViewports;
        p11DeviceFuncs->pfnSetScissorRects                           = ddi10SetScissorRects;
        p11DeviceFuncs->pfnClearRenderTargetView                     = ddi10ClearRenderTargetView;
        p11DeviceFuncs->pfnClearDepthStencilView                     = ddi10ClearDepthStencilView;
        p11DeviceFuncs->pfnSetPredication                            = ddi10SetPredication;
        p11DeviceFuncs->pfnQueryGetData                              = ddi10QueryGetData;
        p11DeviceFuncs->pfnFlush                                     = ddi10Flush;
        p11DeviceFuncs->pfnGenMips                                   = ddi10GenMips;
        p11DeviceFuncs->pfnResourceCopy                              = ddi10ResourceCopy;
        p11DeviceFuncs->pfnResourceResolveSubresource                = ddi10ResourceResolveSubresource;

        /* Infrequent paths. */
        p11DeviceFuncs->pfnResourceMap                               = ddi10ResourceMap;
        p11DeviceFuncs->pfnResourceUnmap                             = ddi10ResourceUnmap;
        p11DeviceFuncs->pfnResourceIsStagingBusy                     = vboxDXResourceIsStagingBusy;
        p11DeviceFuncs->pfnRelocateDeviceFuncs                       = ddi11RelocateDeviceFuncs;
        p11DeviceFuncs->pfnCalcPrivateResourceSize                   = ddi11CalcPrivateResourceSize;
        p11DeviceFuncs->pfnCalcPrivateOpenedResourceSize             = ddi10CalcPrivateOpenedResourceSize;
        p11DeviceFuncs->pfnCreateResource                            = ddi11CreateResource;
        p11DeviceFuncs->pfnOpenResource                              = ddi10OpenResource;
        p11DeviceFuncs->pfnDestroyResource                           = ddi10DestroyResource;
        p11DeviceFuncs->pfnCalcPrivateShaderResourceViewSize         = ddi11CalcPrivateShaderResourceViewSize;
        p11DeviceFuncs->pfnCreateShaderResourceView                  = ddi11CreateShaderResourceView;
        p11DeviceFuncs->pfnDestroyShaderResourceView                 = ddi10DestroyShaderResourceView;
        p11DeviceFuncs->pfnCalcPrivateRenderTargetViewSize           = ddi10CalcPrivateRenderTargetViewSize;
        p11DeviceFuncs->pfnCreateRenderTargetView                    = ddi10CreateRenderTargetView;
        p11DeviceFuncs->pfnDestroyRenderTargetView                   = ddi10DestroyRenderTargetView;
        p11DeviceFuncs->pfnCalcPrivateDepthStencilViewSize           = ddi11CalcPrivateDepthStencilViewSize;
        p11DeviceFuncs->pfnCreateDepthStencilView                    = ddi11CreateDepthStencilView;
        p11DeviceFuncs->pfnDestroyDepthStencilView                   = ddi10DestroyDepthStencilView;
        p11DeviceFuncs->pfnCalcPrivateElementLayoutSize              = ddi10CalcPrivateElementLayoutSize;
        p11DeviceFuncs->pfnCreateElementLayout                       = ddi10CreateElementLayout;
        p11DeviceFuncs->pfnDestroyElementLayout                      = ddi10DestroyElementLayout;
        p11DeviceFuncs->pfnCalcPrivateBlendStateSize                 = ddi10_1CalcPrivateBlendStateSize;
        p11DeviceFuncs->pfnCreateBlendState                          = ddi10_1CreateBlendState;
        p11DeviceFuncs->pfnDestroyBlendState                         = ddi10DestroyBlendState;
        p11DeviceFuncs->pfnCalcPrivateDepthStencilStateSize          = ddi10CalcPrivateDepthStencilStateSize;
        p11DeviceFuncs->pfnCreateDepthStencilState                   = ddi10CreateDepthStencilState;
        p11DeviceFuncs->pfnDestroyDepthStencilState                  = ddi10DestroyDepthStencilState;
        p11DeviceFuncs->pfnCalcPrivateRasterizerStateSize            = ddi10CalcPrivateRasterizerStateSize;
        p11DeviceFuncs->pfnCreateRasterizerState                     = ddi10CreateRasterizerState;
        p11DeviceFuncs->pfnDestroyRasterizerState                    = ddi10DestroyRasterizerState;
        p11DeviceFuncs->pfnCalcPrivateShaderSize                     = ddi10CalcPrivateShaderSize;
        p11DeviceFuncs->pfnCreateVertexShader                        = ddi10CreateVertexShader;
        p11DeviceFuncs->pfnCreateGeometryShader                      = ddi10CreateGeometryShader;
        p11DeviceFuncs->pfnCreatePixelShader                         = ddi10CreatePixelShader;
        p11DeviceFuncs->pfnCalcPrivateGeometryShaderWithStreamOutput = ddi11CalcPrivateGeometryShaderWithStreamOutput;
        p11DeviceFuncs->pfnCreateGeometryShaderWithStreamOutput      = ddi11CreateGeometryShaderWithStreamOutput;
        p11DeviceFuncs->pfnDestroyShader                             = ddi10DestroyShader;
        p11DeviceFuncs->pfnCalcPrivateSamplerSize                    = ddi10CalcPrivateSamplerSize;
        p11DeviceFuncs->pfnCreateSampler                             = ddi10CreateSampler;
        p11DeviceFuncs->pfnDestroySampler                            = ddi10DestroySampler;
        p11DeviceFuncs->pfnCalcPrivateQuerySize                      = ddi10CalcPrivateQuerySize;
        p11DeviceFuncs->pfnCreateQuery                               = ddi10CreateQuery;
        p11DeviceFuncs->pfnDestroyQuery                              = ddi10DestroyQuery;

        p11DeviceFuncs->pfnCheckFormatSupport                        = vboxDXCheckFormatSupport;
        p11DeviceFuncs->pfnCheckMultisampleQualityLevels             = vboxDXCheckMultisampleQualityLevels;
        p11DeviceFuncs->pfnCheckCounterInfo                          = ddi10CheckCounterInfo;
        p11DeviceFuncs->pfnCheckCounter                              = ddi10CheckCounter;

        p11DeviceFuncs->pfnDestroyDevice                             = ddi10DestroyDevice;
        p11DeviceFuncs->pfnSetTextFilterSize                         = ddi10SetTextFilterSize;

        /* Additional 10.1 entries */
        p11DeviceFuncs->pfnResourceConvert                           = vboxDXResourceConvert;
        p11DeviceFuncs->pfnResourceConvertRegion                     = ddi10ResourceConvertRegion;

        /* Additional 11.0 entries */
        p11DeviceFuncs->pfnDrawIndexedInstancedIndirect              = ddi11DrawIndexedInstancedIndirect;
        p11DeviceFuncs->pfnDrawInstancedIndirect                     = ddi11DrawInstancedIndirect;
        p11DeviceFuncs->pfnCommandListExecute                        = 0;
        p11DeviceFuncs->pfnHsSetShaderResources                      = ddi10HsSetShaderResources;
        p11DeviceFuncs->pfnHsSetShader                               = ddi10HsSetShader;
        p11DeviceFuncs->pfnHsSetSamplers                             = ddi10HsSetSamplers;
        p11DeviceFuncs->pfnHsSetConstantBuffers                      = ddi10HsSetConstantBuffers;
        p11DeviceFuncs->pfnDsSetShaderResources                      = ddi10DsSetShaderResources;
        p11DeviceFuncs->pfnDsSetShader                               = ddi10DsSetShader;
        p11DeviceFuncs->pfnDsSetSamplers                             = ddi10DsSetSamplers;
        p11DeviceFuncs->pfnDsSetConstantBuffers                      = ddi10DsSetConstantBuffers;
        p11DeviceFuncs->pfnCreateHullShader                          = ddi11CreateHullShader;
        p11DeviceFuncs->pfnCreateDomainShader                        = ddi11CreateDomainShader;
        p11DeviceFuncs->pfnCheckDeferredContextHandleSizes           = 0;
        p11DeviceFuncs->pfnCalcDeferredContextHandleSize             = 0;
        p11DeviceFuncs->pfnCalcPrivateDeferredContextSize            = 0;
        p11DeviceFuncs->pfnCreateDeferredContext                     = 0;
        p11DeviceFuncs->pfnAbandonCommandList                        = 0;
        p11DeviceFuncs->pfnCalcPrivateCommandListSize                = 0;
        p11DeviceFuncs->pfnCreateCommandList                         = 0;
        p11DeviceFuncs->pfnDestroyCommandList                        = 0;
        p11DeviceFuncs->pfnCalcPrivateTessellationShaderSize         = ddi11CalcPrivateTessellationShaderSize;
        p11DeviceFuncs->pfnPsSetShaderWithIfaces                     = vboxDXPsSetShaderWithIfaces;
        p11DeviceFuncs->pfnVsSetShaderWithIfaces                     = vboxDXVsSetShaderWithIfaces;
        p11DeviceFuncs->pfnGsSetShaderWithIfaces                     = vboxDXGsSetShaderWithIfaces;
        p11DeviceFuncs->pfnHsSetShaderWithIfaces                     = vboxDXHsSetShaderWithIfaces;
        p11DeviceFuncs->pfnDsSetShaderWithIfaces                     = vboxDXDsSetShaderWithIfaces;
        p11DeviceFuncs->pfnCsSetShaderWithIfaces                     = vboxDXCsSetShaderWithIfaces;
        p11DeviceFuncs->pfnCreateComputeShader                       = ddi11CreateComputeShader;
        p11DeviceFuncs->pfnCsSetShader                               = ddi10CsSetShader;
        p11DeviceFuncs->pfnCsSetShaderResources                      = ddi10CsSetShaderResources;
        p11DeviceFuncs->pfnCsSetSamplers                             = ddi10CsSetSamplers;
        p11DeviceFuncs->pfnCsSetConstantBuffers                      = ddi10CsSetConstantBuffers;
        p11DeviceFuncs->pfnCalcPrivateUnorderedAccessViewSize        = ddi11CalcPrivateUnorderedAccessViewSize;
        p11DeviceFuncs->pfnCreateUnorderedAccessView                 = ddi11CreateUnorderedAccessView;
        p11DeviceFuncs->pfnDestroyUnorderedAccessView                = ddi11DestroyUnorderedAccessView;
        p11DeviceFuncs->pfnClearUnorderedAccessViewUint              = ddi11ClearUnorderedAccessViewUint;
        p11DeviceFuncs->pfnClearUnorderedAccessViewFloat             = ddi11ClearUnorderedAccessViewFloat;
        p11DeviceFuncs->pfnCsSetUnorderedAccessViews                 = ddi11CsSetUnorderedAccessViews;
        p11DeviceFuncs->pfnDispatch                                  = ddi11Dispatch;
        p11DeviceFuncs->pfnDispatchIndirect                          = ddi11DispatchIndirect;
        p11DeviceFuncs->pfnSetResourceMinLOD                         = vboxDXSetResourceMinLOD;
        p11DeviceFuncs->pfnCopyStructureCount                        = ddi11CopyStructureCount;
        p11DeviceFuncs->pfnRecycleCommandList                        = 0;
        p11DeviceFuncs->pfnRecycleCreateCommandList                  = 0;
        p11DeviceFuncs->pfnRecycleCreateDeferredContext              = 0;
        p11DeviceFuncs->pfnRecycleDestroyCommandList                 = 0;
    }
    else if (pCreateData->Interface == D3D10_1_DDI_INTERFACE_VERSION)
    {
        /*
         * 10.1
         */
        D3D10_1DDI_DEVICEFUNCS *p10_1DeviceFuncs = pCreateData->p10_1DeviceFuncs;

        /* Order of functions is in decreasing order of priority ( as far as performance is concerned ). */
        /* High frequency functions. */
        p10_1DeviceFuncs->pfnDefaultConstantBufferUpdateSubresourceUP  = ddi10DefaultConstantBufferUpdateSubresourceUP;
        p10_1DeviceFuncs->pfnVsSetConstantBuffers                      = ddi10VsSetConstantBuffers;
        p10_1DeviceFuncs->pfnPsSetShaderResources                      = ddi10PsSetShaderResources;
        p10_1DeviceFuncs->pfnPsSetShader                               = ddi10PsSetShader;
        p10_1DeviceFuncs->pfnPsSetSamplers                             = ddi10PsSetSamplers;
        p10_1DeviceFuncs->pfnVsSetShader                               = ddi10VsSetShader;
        p10_1DeviceFuncs->pfnDrawIndexed                               = ddi10DrawIndexed;
        p10_1DeviceFuncs->pfnDraw                                      = ddi10Draw;
        p10_1DeviceFuncs->pfnDynamicIABufferMapNoOverwrite             = ddi10DynamicIABufferMapNoOverwrite;
        p10_1DeviceFuncs->pfnDynamicIABufferUnmap                      = ddi10DynamicIABufferUnmap;
        p10_1DeviceFuncs->pfnDynamicConstantBufferMapDiscard           = ddi10DynamicConstantBufferMapDiscard;
        p10_1DeviceFuncs->pfnDynamicIABufferMapDiscard                 = ddi10DynamicIABufferMapDiscard;
        p10_1DeviceFuncs->pfnDynamicConstantBufferUnmap                = ddi10DynamicConstantBufferUnmap;
        p10_1DeviceFuncs->pfnPsSetConstantBuffers                      = ddi10PsSetConstantBuffers;
        p10_1DeviceFuncs->pfnIaSetInputLayout                          = ddi10IaSetInputLayout;
        p10_1DeviceFuncs->pfnIaSetVertexBuffers                        = ddi10IaSetVertexBuffers;
        p10_1DeviceFuncs->pfnIaSetIndexBuffer                          = ddi10IaSetIndexBuffer;

        /* Middle frequency functions. */
        p10_1DeviceFuncs->pfnDrawIndexedInstanced                      = ddi10DrawIndexedInstanced;
        p10_1DeviceFuncs->pfnDrawInstanced                             = ddi10DrawInstanced;
        p10_1DeviceFuncs->pfnDynamicResourceMapDiscard                 = ddi10DynamicResourceMapDiscard;
        p10_1DeviceFuncs->pfnDynamicResourceUnmap                      = ddi10DynamicResourceUnmap;
        p10_1DeviceFuncs->pfnGsSetConstantBuffers                      = ddi10GsSetConstantBuffers;
        p10_1DeviceFuncs->pfnGsSetShader                               = ddi10GsSetShader;
        p10_1DeviceFuncs->pfnIaSetTopology                             = ddi10IaSetTopology;
        p10_1DeviceFuncs->pfnStagingResourceMap                        = ddi10StagingResourceMap;
        p10_1DeviceFuncs->pfnStagingResourceUnmap                      = ddi10StagingResourceUnmap;
        p10_1DeviceFuncs->pfnVsSetShaderResources                      = ddi10VsSetShaderResources;
        p10_1DeviceFuncs->pfnVsSetSamplers                             = ddi10VsSetSamplers;
        p10_1DeviceFuncs->pfnGsSetShaderResources                      = ddi10GsSetShaderResources;
        p10_1DeviceFuncs->pfnGsSetSamplers                             = ddi10GsSetSamplers;
        p10_1DeviceFuncs->pfnSetRenderTargets                          = ddi10SetRenderTargets;
        p10_1DeviceFuncs->pfnShaderResourceViewReadAfterWriteHazard    = ddi10ShaderResourceViewReadAfterWriteHazard;
        p10_1DeviceFuncs->pfnResourceReadAfterWriteHazard              = ddi10ResourceReadAfterWriteHazard;
        p10_1DeviceFuncs->pfnSetBlendState                             = ddi10SetBlendState;
        p10_1DeviceFuncs->pfnSetDepthStencilState                      = ddi10SetDepthStencilState;
        p10_1DeviceFuncs->pfnSetRasterizerState                        = ddi10SetRasterizerState;
        p10_1DeviceFuncs->pfnQueryEnd                                  = ddi10QueryEnd;
        p10_1DeviceFuncs->pfnQueryBegin                                = ddi10QueryBegin;
        p10_1DeviceFuncs->pfnResourceCopyRegion                        = ddi10ResourceCopyRegion;
        p10_1DeviceFuncs->pfnResourceUpdateSubresourceUP               = ddi10ResourceUpdateSubresourceUP;
        p10_1DeviceFuncs->pfnSoSetTargets                              = ddi10SoSetTargets;
        p10_1DeviceFuncs->pfnDrawAuto                                  = ddi10DrawAuto;
        p10_1DeviceFuncs->pfnSetViewports                              = ddi10SetViewports;
        p10_1DeviceFuncs->pfnSetScissorRects                           = ddi10SetScissorRects;
        p10_1DeviceFuncs->pfnClearRenderTargetView                     = ddi10ClearRenderTargetView;
        p10_1DeviceFuncs->pfnClearDepthStencilView                     = ddi10ClearDepthStencilView;
        p10_1DeviceFuncs->pfnSetPredication                            = ddi10SetPredication;
        p10_1DeviceFuncs->pfnQueryGetData                              = ddi10QueryGetData;
        p10_1DeviceFuncs->pfnFlush                                     = ddi10Flush;
        p10_1DeviceFuncs->pfnGenMips                                   = ddi10GenMips;
        p10_1DeviceFuncs->pfnResourceCopy                              = ddi10ResourceCopy;
        p10_1DeviceFuncs->pfnResourceResolveSubresource                = ddi10ResourceResolveSubresource;

        /* Infrequent paths. */
        p10_1DeviceFuncs->pfnResourceMap                               = ddi10ResourceMap;
        p10_1DeviceFuncs->pfnResourceUnmap                             = ddi10ResourceUnmap;
        p10_1DeviceFuncs->pfnResourceIsStagingBusy                     = vboxDXResourceIsStagingBusy;
        p10_1DeviceFuncs->pfnRelocateDeviceFuncs                       = ddi10_1RelocateDeviceFuncs;
        p10_1DeviceFuncs->pfnCalcPrivateResourceSize                   = ddi10CalcPrivateResourceSize;
        p10_1DeviceFuncs->pfnCalcPrivateOpenedResourceSize             = ddi10CalcPrivateOpenedResourceSize;
        p10_1DeviceFuncs->pfnCreateResource                            = ddi10CreateResource;
        p10_1DeviceFuncs->pfnOpenResource                              = ddi10OpenResource;
        p10_1DeviceFuncs->pfnDestroyResource                           = ddi10DestroyResource;
        p10_1DeviceFuncs->pfnCalcPrivateShaderResourceViewSize         = ddi10_1CalcPrivateShaderResourceViewSize;
        p10_1DeviceFuncs->pfnCreateShaderResourceView                  = ddi10_1CreateShaderResourceView;
        p10_1DeviceFuncs->pfnDestroyShaderResourceView                 = ddi10DestroyShaderResourceView;
        p10_1DeviceFuncs->pfnCalcPrivateRenderTargetViewSize           = ddi10CalcPrivateRenderTargetViewSize;
        p10_1DeviceFuncs->pfnCreateRenderTargetView                    = ddi10CreateRenderTargetView;
        p10_1DeviceFuncs->pfnDestroyRenderTargetView                   = ddi10DestroyRenderTargetView;
        p10_1DeviceFuncs->pfnCalcPrivateDepthStencilViewSize           = ddi10CalcPrivateDepthStencilViewSize;
        p10_1DeviceFuncs->pfnCreateDepthStencilView                    = ddi10CreateDepthStencilView;
        p10_1DeviceFuncs->pfnDestroyDepthStencilView                   = ddi10DestroyDepthStencilView;
        p10_1DeviceFuncs->pfnCalcPrivateElementLayoutSize              = ddi10CalcPrivateElementLayoutSize;
        p10_1DeviceFuncs->pfnCreateElementLayout                       = ddi10CreateElementLayout;
        p10_1DeviceFuncs->pfnDestroyElementLayout                      = ddi10DestroyElementLayout;
        p10_1DeviceFuncs->pfnCalcPrivateBlendStateSize                 = ddi10_1CalcPrivateBlendStateSize;
        p10_1DeviceFuncs->pfnCreateBlendState                          = ddi10_1CreateBlendState;
        p10_1DeviceFuncs->pfnDestroyBlendState                         = ddi10DestroyBlendState;
        p10_1DeviceFuncs->pfnCalcPrivateDepthStencilStateSize          = ddi10CalcPrivateDepthStencilStateSize;
        p10_1DeviceFuncs->pfnCreateDepthStencilState                   = ddi10CreateDepthStencilState;
        p10_1DeviceFuncs->pfnDestroyDepthStencilState                  = ddi10DestroyDepthStencilState;
        p10_1DeviceFuncs->pfnCalcPrivateRasterizerStateSize            = ddi10CalcPrivateRasterizerStateSize;
        p10_1DeviceFuncs->pfnCreateRasterizerState                     = ddi10CreateRasterizerState;
        p10_1DeviceFuncs->pfnDestroyRasterizerState                    = ddi10DestroyRasterizerState;
        p10_1DeviceFuncs->pfnCalcPrivateShaderSize                     = ddi10CalcPrivateShaderSize;
        p10_1DeviceFuncs->pfnCreateVertexShader                        = ddi10CreateVertexShader;
        p10_1DeviceFuncs->pfnCreateGeometryShader                      = ddi10CreateGeometryShader;
        p10_1DeviceFuncs->pfnCreatePixelShader                         = ddi10CreatePixelShader;
        p10_1DeviceFuncs->pfnCalcPrivateGeometryShaderWithStreamOutput = ddi10CalcPrivateGeometryShaderWithStreamOutput;
        p10_1DeviceFuncs->pfnCreateGeometryShaderWithStreamOutput      = ddi10CreateGeometryShaderWithStreamOutput;
        p10_1DeviceFuncs->pfnDestroyShader                             = ddi10DestroyShader;
        p10_1DeviceFuncs->pfnCalcPrivateSamplerSize                    = ddi10CalcPrivateSamplerSize;
        p10_1DeviceFuncs->pfnCreateSampler                             = ddi10CreateSampler;
        p10_1DeviceFuncs->pfnDestroySampler                            = ddi10DestroySampler;
        p10_1DeviceFuncs->pfnCalcPrivateQuerySize                      = ddi10CalcPrivateQuerySize;
        p10_1DeviceFuncs->pfnCreateQuery                               = ddi10CreateQuery;
        p10_1DeviceFuncs->pfnDestroyQuery                              = ddi10DestroyQuery;

        p10_1DeviceFuncs->pfnCheckFormatSupport                        = vboxDXCheckFormatSupport;
        p10_1DeviceFuncs->pfnCheckMultisampleQualityLevels             = vboxDXCheckMultisampleQualityLevels;
        p10_1DeviceFuncs->pfnCheckCounterInfo                          = ddi10CheckCounterInfo;
        p10_1DeviceFuncs->pfnCheckCounter                              = ddi10CheckCounter;

        p10_1DeviceFuncs->pfnDestroyDevice                             = ddi10DestroyDevice;
        p10_1DeviceFuncs->pfnSetTextFilterSize                         = ddi10SetTextFilterSize;

        /* Additional 10.1 entries */
        p10_1DeviceFuncs->pfnResourceConvert                           = vboxDXResourceConvert;
        p10_1DeviceFuncs->pfnResourceConvertRegion                     = ddi10ResourceConvertRegion;
    }
    else
    {
        /*
         * 10.0
         */
        D3D10DDI_DEVICEFUNCS *p10DeviceFuncs = pCreateData->pDeviceFuncs;

        /* Order of functions is in decreasing order of priority ( as far as performance is concerned ). */
        /* High frequency functions. */
        p10DeviceFuncs->pfnDefaultConstantBufferUpdateSubresourceUP  = ddi10DefaultConstantBufferUpdateSubresourceUP;
        p10DeviceFuncs->pfnVsSetConstantBuffers                      = ddi10VsSetConstantBuffers;
        p10DeviceFuncs->pfnPsSetShaderResources                      = ddi10PsSetShaderResources;
        p10DeviceFuncs->pfnPsSetShader                               = ddi10PsSetShader;
        p10DeviceFuncs->pfnPsSetSamplers                             = ddi10PsSetSamplers;
        p10DeviceFuncs->pfnVsSetShader                               = ddi10VsSetShader;
        p10DeviceFuncs->pfnDrawIndexed                               = ddi10DrawIndexed;
        p10DeviceFuncs->pfnDraw                                      = ddi10Draw;
        p10DeviceFuncs->pfnDynamicIABufferMapNoOverwrite             = ddi10DynamicIABufferMapNoOverwrite;
        p10DeviceFuncs->pfnDynamicIABufferUnmap                      = ddi10DynamicIABufferUnmap;
        p10DeviceFuncs->pfnDynamicConstantBufferMapDiscard           = ddi10DynamicConstantBufferMapDiscard;
        p10DeviceFuncs->pfnDynamicIABufferMapDiscard                 = ddi10DynamicIABufferMapDiscard;
        p10DeviceFuncs->pfnDynamicConstantBufferUnmap                = ddi10DynamicConstantBufferUnmap;
        p10DeviceFuncs->pfnPsSetConstantBuffers                      = ddi10PsSetConstantBuffers;
        p10DeviceFuncs->pfnIaSetInputLayout                          = ddi10IaSetInputLayout;
        p10DeviceFuncs->pfnIaSetVertexBuffers                        = ddi10IaSetVertexBuffers;
        p10DeviceFuncs->pfnIaSetIndexBuffer                          = ddi10IaSetIndexBuffer;

        /* Middle frequency functions. */
        p10DeviceFuncs->pfnDrawIndexedInstanced                      = ddi10DrawIndexedInstanced;
        p10DeviceFuncs->pfnDrawInstanced                             = ddi10DrawInstanced;
        p10DeviceFuncs->pfnDynamicResourceMapDiscard                 = ddi10DynamicResourceMapDiscard;
        p10DeviceFuncs->pfnDynamicResourceUnmap                      = ddi10DynamicResourceUnmap;
        p10DeviceFuncs->pfnGsSetConstantBuffers                      = ddi10GsSetConstantBuffers;
        p10DeviceFuncs->pfnGsSetShader                               = ddi10GsSetShader;
        p10DeviceFuncs->pfnIaSetTopology                             = ddi10IaSetTopology;
        p10DeviceFuncs->pfnStagingResourceMap                        = ddi10StagingResourceMap;
        p10DeviceFuncs->pfnStagingResourceUnmap                      = ddi10StagingResourceUnmap;
        p10DeviceFuncs->pfnVsSetShaderResources                      = ddi10VsSetShaderResources;
        p10DeviceFuncs->pfnVsSetSamplers                             = ddi10VsSetSamplers;
        p10DeviceFuncs->pfnGsSetShaderResources                      = ddi10GsSetShaderResources;
        p10DeviceFuncs->pfnGsSetSamplers                             = ddi10GsSetSamplers;
        p10DeviceFuncs->pfnSetRenderTargets                          = ddi10SetRenderTargets;
        p10DeviceFuncs->pfnShaderResourceViewReadAfterWriteHazard    = ddi10ShaderResourceViewReadAfterWriteHazard;
        p10DeviceFuncs->pfnResourceReadAfterWriteHazard              = ddi10ResourceReadAfterWriteHazard;
        p10DeviceFuncs->pfnSetBlendState                             = ddi10SetBlendState;
        p10DeviceFuncs->pfnSetDepthStencilState                      = ddi10SetDepthStencilState;
        p10DeviceFuncs->pfnSetRasterizerState                        = ddi10SetRasterizerState;
        p10DeviceFuncs->pfnQueryEnd                                  = ddi10QueryEnd;
        p10DeviceFuncs->pfnQueryBegin                                = ddi10QueryBegin;
        p10DeviceFuncs->pfnResourceCopyRegion                        = ddi10ResourceCopyRegion;
        p10DeviceFuncs->pfnResourceUpdateSubresourceUP               = ddi10ResourceUpdateSubresourceUP;
        p10DeviceFuncs->pfnSoSetTargets                              = ddi10SoSetTargets;
        p10DeviceFuncs->pfnDrawAuto                                  = ddi10DrawAuto;
        p10DeviceFuncs->pfnSetViewports                              = ddi10SetViewports;
        p10DeviceFuncs->pfnSetScissorRects                           = ddi10SetScissorRects;
        p10DeviceFuncs->pfnClearRenderTargetView                     = ddi10ClearRenderTargetView;
        p10DeviceFuncs->pfnClearDepthStencilView                     = ddi10ClearDepthStencilView;
        p10DeviceFuncs->pfnSetPredication                            = ddi10SetPredication;
        p10DeviceFuncs->pfnQueryGetData                              = ddi10QueryGetData;
        p10DeviceFuncs->pfnFlush                                     = ddi10Flush;
        p10DeviceFuncs->pfnGenMips                                   = ddi10GenMips;
        p10DeviceFuncs->pfnResourceCopy                              = ddi10ResourceCopy;
        p10DeviceFuncs->pfnResourceResolveSubresource                = ddi10ResourceResolveSubresource;

        /* Infrequent paths. */
        p10DeviceFuncs->pfnResourceMap                               = ddi10ResourceMap;
        p10DeviceFuncs->pfnResourceUnmap                             = ddi10ResourceUnmap;
        p10DeviceFuncs->pfnResourceIsStagingBusy                     = vboxDXResourceIsStagingBusy;
        p10DeviceFuncs->pfnRelocateDeviceFuncs                       = ddi10RelocateDeviceFuncs;
        p10DeviceFuncs->pfnCalcPrivateResourceSize                   = ddi10CalcPrivateResourceSize;
        p10DeviceFuncs->pfnCalcPrivateOpenedResourceSize             = ddi10CalcPrivateOpenedResourceSize;
        p10DeviceFuncs->pfnCreateResource                            = ddi10CreateResource;
        p10DeviceFuncs->pfnOpenResource                              = ddi10OpenResource;
        p10DeviceFuncs->pfnDestroyResource                           = ddi10DestroyResource;
        p10DeviceFuncs->pfnCalcPrivateShaderResourceViewSize         = ddi10CalcPrivateShaderResourceViewSize;
        p10DeviceFuncs->pfnCreateShaderResourceView                  = ddi10CreateShaderResourceView;
        p10DeviceFuncs->pfnDestroyShaderResourceView                 = ddi10DestroyShaderResourceView;
        p10DeviceFuncs->pfnCalcPrivateRenderTargetViewSize           = ddi10CalcPrivateRenderTargetViewSize;
        p10DeviceFuncs->pfnCreateRenderTargetView                    = ddi10CreateRenderTargetView;
        p10DeviceFuncs->pfnDestroyRenderTargetView                   = ddi10DestroyRenderTargetView;
        p10DeviceFuncs->pfnCalcPrivateDepthStencilViewSize           = ddi10CalcPrivateDepthStencilViewSize;
        p10DeviceFuncs->pfnCreateDepthStencilView                    = ddi10CreateDepthStencilView;
        p10DeviceFuncs->pfnDestroyDepthStencilView                   = ddi10DestroyDepthStencilView;
        p10DeviceFuncs->pfnCalcPrivateElementLayoutSize              = ddi10CalcPrivateElementLayoutSize;
        p10DeviceFuncs->pfnCreateElementLayout                       = ddi10CreateElementLayout;
        p10DeviceFuncs->pfnDestroyElementLayout                      = ddi10DestroyElementLayout;
        p10DeviceFuncs->pfnCalcPrivateBlendStateSize                 = ddi10CalcPrivateBlendStateSize;
        p10DeviceFuncs->pfnCreateBlendState                          = ddi10CreateBlendState;
        p10DeviceFuncs->pfnDestroyBlendState                         = ddi10DestroyBlendState;
        p10DeviceFuncs->pfnCalcPrivateDepthStencilStateSize          = ddi10CalcPrivateDepthStencilStateSize;
        p10DeviceFuncs->pfnCreateDepthStencilState                   = ddi10CreateDepthStencilState;
        p10DeviceFuncs->pfnDestroyDepthStencilState                  = ddi10DestroyDepthStencilState;
        p10DeviceFuncs->pfnCalcPrivateRasterizerStateSize            = ddi10CalcPrivateRasterizerStateSize;
        p10DeviceFuncs->pfnCreateRasterizerState                     = ddi10CreateRasterizerState;
        p10DeviceFuncs->pfnDestroyRasterizerState                    = ddi10DestroyRasterizerState;
        p10DeviceFuncs->pfnCalcPrivateShaderSize                     = ddi10CalcPrivateShaderSize;
        p10DeviceFuncs->pfnCreateVertexShader                        = ddi10CreateVertexShader;
        p10DeviceFuncs->pfnCreateGeometryShader                      = ddi10CreateGeometryShader;
        p10DeviceFuncs->pfnCreatePixelShader                         = ddi10CreatePixelShader;
        p10DeviceFuncs->pfnCalcPrivateGeometryShaderWithStreamOutput = ddi10CalcPrivateGeometryShaderWithStreamOutput;
        p10DeviceFuncs->pfnCreateGeometryShaderWithStreamOutput      = ddi10CreateGeometryShaderWithStreamOutput;
        p10DeviceFuncs->pfnDestroyShader                             = ddi10DestroyShader;
        p10DeviceFuncs->pfnCalcPrivateSamplerSize                    = ddi10CalcPrivateSamplerSize;
        p10DeviceFuncs->pfnCreateSampler                             = ddi10CreateSampler;
        p10DeviceFuncs->pfnDestroySampler                            = ddi10DestroySampler;
        p10DeviceFuncs->pfnCalcPrivateQuerySize                      = ddi10CalcPrivateQuerySize;
        p10DeviceFuncs->pfnCreateQuery                               = ddi10CreateQuery;
        p10DeviceFuncs->pfnDestroyQuery                              = ddi10DestroyQuery;

        p10DeviceFuncs->pfnCheckFormatSupport                        = vboxDXCheckFormatSupport;
        p10DeviceFuncs->pfnCheckMultisampleQualityLevels             = vboxDXCheckMultisampleQualityLevels;
        p10DeviceFuncs->pfnCheckCounterInfo                          = ddi10CheckCounterInfo;
        p10DeviceFuncs->pfnCheckCounter                              = ddi10CheckCounter;

        p10DeviceFuncs->pfnDestroyDevice                             = ddi10DestroyDevice;
        p10DeviceFuncs->pfnSetTextFilterSize                         = ddi10SetTextFilterSize;
    }

    /* DXGI functions. */
    if (IS_DXGI1_2_BASE_FUNCTIONS(pCreateData->Interface, pCreateData->Version))
    {
        DXGI1_2_DDI_BASE_FUNCTIONS *pDXGIFuncs = pCreateData->DXGIBaseDDI.pDXGIDDIBaseFunctions3;
        pDXGIFuncs->pfnPresent = dxgiPresent;
        pDXGIFuncs->pfnGetGammaCaps = dxgiGetGammaCaps;
        pDXGIFuncs->pfnSetDisplayMode = dxgiSetDisplayMode;
        pDXGIFuncs->pfnSetResourcePriority = vboxDXGISetResourcePriority;
        pDXGIFuncs->pfnQueryResourceResidency = dxgiQueryResourceResidency;
        pDXGIFuncs->pfnRotateResourceIdentities = dxgiRotateResourceIdentities;
        pDXGIFuncs->pfnBlt = dxgiBlt;
        pDXGIFuncs->pfnResolveSharedResource = dxgiResolveSharedResource;
        pDXGIFuncs->pfnBlt1 = vboxDXGIBlt1;
        pDXGIFuncs->pfnOfferResources = dxgiOfferResources;
        pDXGIFuncs->pfnReclaimResources = dxgiReclaimResources;

        if (IS_DXGI_MULTIPLANE_OVERLAY_FUNCTIONS(pCreateData->Interface, pCreateData->Version))
        {
            // TBD: Implement MultiplaneOverlay callbacks
        }
    }
    else if (IS_DXGI1_1_BASE_FUNCTIONS(pCreateData->Interface, pCreateData->Version))
    {
        DXGI1_1_DDI_BASE_FUNCTIONS *pDXGIFuncs = pCreateData->DXGIBaseDDI.pDXGIDDIBaseFunctions2;
        pDXGIFuncs->pfnPresent = dxgiPresent;
        pDXGIFuncs->pfnGetGammaCaps = dxgiGetGammaCaps;
        pDXGIFuncs->pfnSetDisplayMode = dxgiSetDisplayMode;
        pDXGIFuncs->pfnSetResourcePriority = vboxDXGISetResourcePriority;
        pDXGIFuncs->pfnQueryResourceResidency = dxgiQueryResourceResidency;
        pDXGIFuncs->pfnRotateResourceIdentities = dxgiRotateResourceIdentities;
        pDXGIFuncs->pfnBlt = dxgiBlt;
        pDXGIFuncs->pfnResolveSharedResource = dxgiResolveSharedResource;
    }
    else
    {
        DXGI_DDI_BASE_FUNCTIONS *pDXGIFuncs = pCreateData->DXGIBaseDDI.pDXGIDDIBaseFunctions;
        pDXGIFuncs->pfnPresent = dxgiPresent;
        pDXGIFuncs->pfnGetGammaCaps = dxgiGetGammaCaps;
        pDXGIFuncs->pfnSetDisplayMode = dxgiSetDisplayMode;
        pDXGIFuncs->pfnSetResourcePriority = vboxDXGISetResourcePriority;
        pDXGIFuncs->pfnQueryResourceResidency = dxgiQueryResourceResidency;
        pDXGIFuncs->pfnRotateResourceIdentities = dxgiRotateResourceIdentities;
        pDXGIFuncs->pfnBlt = dxgiBlt;
    }

    if (pCreateData->Interface == D3D11_1_DDI_INTERFACE_VERSION)
        *pCreateData->ppfnRetrieveSubObject = ddi10RetrieveSubObject;

    return S_OK;
}

static HRESULT APIENTRY vboxDXCloseAdapter(D3D10DDI_HADAPTER hAdapter)
{
    RT_NOREF(hAdapter);
    LogFlowFuncEnter();

    return S_OK;
}


static HRESULT APIENTRY vboxDXGetSupportedVersions(D3D10DDI_HADAPTER hAdapter, UINT32* puEntries, UINT64* pSupportedDDIInterfaceVersions)
{
    RT_NOREF(hAdapter);
    LogFlowFuncEnter();

    if (puEntries)
        *puEntries = RT_ELEMENTS(g_aSupportedDDIInterfaceVersions);

    if (pSupportedDDIInterfaceVersions)
    {
        for (unsigned i = 0; i < RT_ELEMENTS(g_aSupportedDDIInterfaceVersions); ++i)
            pSupportedDDIInterfaceVersions[i] = g_aSupportedDDIInterfaceVersions[i];
    }

    return S_OK;
}

static HRESULT APIENTRY vboxDXGetCaps(D3D10DDI_HADAPTER hAdapter, const D3D10_2DDIARG_GETCAPS* pArg)
{
    //DEBUG_BREAKPOINT_TEST();
    PVBOXDXADAPTER pAdapter = (PVBOXDXADAPTER)hAdapter.pDrvPrivate;
    RT_NOREF(pAdapter);
    LogFlow(("vboxDXGetCaps: Type %d", pArg->Type));

    switch (pArg->Type)
    {
        case D3D11DDICAPS_THREADING:
        {
            D3D11DDI_THREADING_CAPS *pCaps = (D3D11DDI_THREADING_CAPS *)pArg->pData;
            pCaps->Caps = 0;
            break;
        }

        case D3D11DDICAPS_SHADER:
        {
            D3D11DDI_SHADER_CAPS *pCaps = (D3D11DDI_SHADER_CAPS *)pArg->pData;
            pCaps->Caps = D3D11DDICAPS_SHADER_COMPUTE_PLUS_RAW_AND_STRUCTURED_BUFFERS_IN_SHADER_4_X;
            break;
        }

        case D3D11_1DDICAPS_D3D11_OPTIONS:
        {
            D3D11_1DDI_D3D11_OPTIONS_DATA *pCaps = (D3D11_1DDI_D3D11_OPTIONS_DATA *)pArg->pData;
            pCaps->OutputMergerLogicOp = TRUE; /* Required for 11.1 driver. */
            pCaps->AssignDebugBinarySupport = FALSE;
            break;
        }

        case D3D11_1DDICAPS_ARCHITECTURE_INFO:
        {
            D3DDDICAPS_ARCHITECTURE_INFO *pCaps = (D3DDDICAPS_ARCHITECTURE_INFO *)pArg->pData;
            pCaps->TileBasedDeferredRenderer = FALSE;
            break;
        }

        case D3D11_1DDICAPS_SHADER_MIN_PRECISION_SUPPORT:
        {
            D3DDDICAPS_SHADER_MIN_PRECISION_SUPPORT *pCaps = (D3DDDICAPS_SHADER_MIN_PRECISION_SUPPORT *)pArg->pData;
            /* The driver supports only the default precision for the shader model, and not a lower precision. */
            pCaps->VertexShaderMinPrecision = 0;
            pCaps->PixelShaderMinPrecision  = 0;
            break;
        }

        case D3D11DDICAPS_3DPIPELINESUPPORT:
        {
            D3D11DDI_3DPIPELINESUPPORT_CAPS *pCaps = (D3D11DDI_3DPIPELINESUPPORT_CAPS *)pArg->pData;

            /* Support of 11.1 pipeline assumes the support of 11.0, 10.1 and 10.0 pipelines. */
            pCaps->Caps =
                D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11DDI_3DPIPELINELEVEL_11_0) |
                D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11DDI_3DPIPELINELEVEL_10_1) |
                D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11DDI_3DPIPELINELEVEL_10_0);
            if (pAdapter->fVBoxCaps & VBSVGA3D_CAP_RASTERIZER_STATE_V2)
                pCaps->Caps |= D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11_1DDI_3DPIPELINELEVEL_11_1);
            break;
        }
        default:
            break;
    }

    return S_OK;
}


HRESULT APIENTRY OpenAdapter10_2(D3D10DDIARG_OPENADAPTER *pOpenData)
{
    //DEBUG_BREAKPOINT_TEST();
    LogFlow(("OpenAdapter10_2: Interface 0x%08x, Version 0x%08x", pOpenData->Interface, pOpenData->Version));

    PVBOXDXADAPTER pAdapter = NULL;

    /* Query the miniport about virtual hardware capabilities. */
    VBOXWDDM_QAI *pAdapterInfo = NULL;
    HRESULT hr = vboxDXQueryAdapterInfo(pOpenData, &pAdapterInfo);
    if (SUCCEEDED(hr))
    {
        hr = vboxDXAdapterInit(pOpenData, pAdapterInfo, &pAdapter);
        if (SUCCEEDED(hr))
        {
            Log(("SUCCESS 3D Enabled, pAdapter (0x%p)", pAdapter));
        }
    }

    if (SUCCEEDED(hr))
    {
        /* Return data to the OS. */
        if (pAdapter->enmHwType == VBOXVIDEO_HWTYPE_VBOX)
        {
            /* Not supposed to work with this. */
            hr = E_FAIL;
        }
        else if (pAdapter->enmHwType == VBOXVIDEO_HWTYPE_VMSVGA)
        {
            pOpenData->hAdapter.pDrvPrivate                      = pAdapter;
            pOpenData->pAdapterFuncs_2->pfnCalcPrivateDeviceSize = vboxDXCalcPrivateDeviceSize;
            pOpenData->pAdapterFuncs_2->pfnCreateDevice          = vboxDXCreateDevice;
            pOpenData->pAdapterFuncs_2->pfnCloseAdapter          = vboxDXCloseAdapter;
            pOpenData->pAdapterFuncs_2->pfnGetSupportedVersions  = vboxDXGetSupportedVersions;
            pOpenData->pAdapterFuncs_2->pfnGetCaps               = vboxDXGetCaps;
        }
        else
            hr = E_FAIL;
    }

    if (FAILED(hr))
    {
        LogRel(("WDDM: WARNING! OpenAdapter10_2 failed hr 0x%x", hr));
        RTMemFree(pAdapter);
    }

    RTMemFree(pAdapterInfo);

    LogFlowFuncLeaveRC(hr);
    return hr;
}


HRESULT APIENTRY OpenAdapter10(D3D10DDIARG_OPENADAPTER *pOpenData)
{
    //DEBUG_BREAKPOINT_TEST();
    LogFlow(("OpenAdapter10: Interface 0x%08x, Version 0x%08x", pOpenData->Interface, pOpenData->Version));

    if (!isInterfaceSupported(pOpenData->Interface))
        return E_FAIL;

    PVBOXDXADAPTER pAdapter = NULL;

    /* Query the miniport about virtual hardware capabilities. */
    VBOXWDDM_QAI *pAdapterInfo = NULL;
    HRESULT hr = vboxDXQueryAdapterInfo(pOpenData, &pAdapterInfo);
    if (SUCCEEDED(hr))
    {
        hr = vboxDXAdapterInit(pOpenData, pAdapterInfo, &pAdapter);
        if (SUCCEEDED(hr))
        {
            Log(("SUCCESS 3D Enabled, pAdapter (0x%p)", pAdapter));
        }
    }

    if (SUCCEEDED(hr))
    {
        /* Return data to the OS. */
        if (pAdapter->enmHwType == VBOXVIDEO_HWTYPE_VBOX)
        {
            /* Not supposed to work with this. */
            hr = E_FAIL;
        }
        else if (pAdapter->enmHwType == VBOXVIDEO_HWTYPE_VMSVGA)
        {
            pOpenData->hAdapter.pDrvPrivate                      = pAdapter;
            pOpenData->pAdapterFuncs->pfnCalcPrivateDeviceSize = vboxDXCalcPrivateDeviceSize;
            pOpenData->pAdapterFuncs->pfnCreateDevice          = vboxDXCreateDevice;
            pOpenData->pAdapterFuncs->pfnCloseAdapter          = vboxDXCloseAdapter;
        }
        else
            hr = E_FAIL;
    }

    if (FAILED(hr))
    {
        LogRel(("WDDM: WARNING! OpenAdapter10 failed hr 0x%x", hr));
        RTMemFree(pAdapter);
    }

    RTMemFree(pAdapterInfo);

    LogFlowFuncLeaveRC(hr);
    return hr;
}


#ifdef DEBUG
/* Verify the function prototype. */
static PFND3D10DDI_OPENADAPTER pOpenAdapter10_2 = OpenAdapter10_2;
static PFND3D10DDI_OPENADAPTER pOpenAdapter10 = OpenAdapter10;

typedef BOOL WINAPI FNGetModuleInformation(HANDLE hProcess, HMODULE hModule, LPMODULEINFO lpmodinfo, DWORD cb);
typedef FNGetModuleInformation *PFNGetModuleInformation;

static PFNGetModuleInformation g_pfnGetModuleInformation = NULL;
static HMODULE g_hModPsapi = NULL;
static PVOID g_VBoxWDbgVEHandler = NULL;

static bool vboxVDbgIsAddressInModule(PVOID pv, const char *pszModuleName)
{
    HMODULE hMod = GetModuleHandleA(pszModuleName);
    if (!hMod)
        return false;

    if (!g_pfnGetModuleInformation)
        return false;

    HANDLE hProcess = GetCurrentProcess();
    MODULEINFO ModuleInfo = {0};
    if (!g_pfnGetModuleInformation(hProcess, hMod, &ModuleInfo, sizeof(ModuleInfo)))
        return false;

    return    (uintptr_t)ModuleInfo.lpBaseOfDll <= (uintptr_t)pv
           && (uintptr_t)pv < (uintptr_t)ModuleInfo.lpBaseOfDll + ModuleInfo.SizeOfImage;
}

static bool vboxVDbgIsExceptionIgnored(PEXCEPTION_RECORD pExceptionRecord)
{
    /* Module (dll) names for GetModuleHandle.
     * Exceptions originated from these modules will be ignored.
     */
    static const char *apszIgnoredModuleNames[] =
    {
        NULL
    };

    int i = 0;
    while (apszIgnoredModuleNames[i])
    {
        if (vboxVDbgIsAddressInModule(pExceptionRecord->ExceptionAddress, apszIgnoredModuleNames[i]))
            return true;

        ++i;
    }

    return false;
}

static LONG WINAPI vboxVDbgVectoredHandler(struct _EXCEPTION_POINTERS *pExceptionInfo) RT_NOTHROW_DEF
{
    static volatile bool g_fAllowIgnore = true; /* Might be changed in kernel debugger. */

    PEXCEPTION_RECORD pExceptionRecord = pExceptionInfo->ExceptionRecord;
    /* PCONTEXT pContextRecord = pExceptionInfo->ContextRecord; */

    switch (pExceptionRecord->ExceptionCode)
    {
        default:
            break;
        case EXCEPTION_BREAKPOINT:
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        case EXCEPTION_FLT_INVALID_OPERATION:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            if (g_fAllowIgnore && vboxVDbgIsExceptionIgnored(pExceptionRecord))
                break;
            ASMBreakpoint();
            break;
        case 0x40010006: /* OutputDebugStringA? */
        case 0x4001000a: /* OutputDebugStringW? */
            break;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void vboxVDbgVEHandlerRegister(void)
{
    Assert(!g_VBoxWDbgVEHandler);
    g_VBoxWDbgVEHandler = AddVectoredExceptionHandler(1, vboxVDbgVectoredHandler);
    Assert(g_VBoxWDbgVEHandler);

    g_hModPsapi = GetModuleHandleA("Psapi.dll"); /* Usually already loaded. */
    if (g_hModPsapi)
        g_pfnGetModuleInformation = (PFNGetModuleInformation)GetProcAddress(g_hModPsapi, "GetModuleInformation");
}

static void vboxVDbgVEHandlerUnregister(void)
{
    Assert(g_VBoxWDbgVEHandler);
    ULONG uResult = RemoveVectoredExceptionHandler(g_VBoxWDbgVEHandler);
    Assert(uResult); RT_NOREF(uResult);
    g_VBoxWDbgVEHandler = NULL;

    g_hModPsapi = NULL;
    g_pfnGetModuleInformation = NULL;
}
#endif /* DEBUG */


/**
 * DLL entry point.
 */
BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
    RT_NOREF(hInstance, lpReserved);

    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:
        {
#ifdef DEBUG
            vboxVDbgVEHandlerRegister();
#endif
            D3DKMTLoad(); /* For logging via the miniport driver. */

            int rc = RTR3InitDll(RTR3INIT_FLAGS_UNOBTRUSIVE);
            AssertRC(rc);
            if (RT_SUCCESS(rc))
            {
                /* Create a logger. Ignore failure to do so. */
                static const char * const s_apszGroups[] = VBOX_LOGGROUP_NAMES;
                PRTLOGGER pLogger;
                int rc2 = RTLogCreate(&pLogger, RTLOGFLAGS_USECRLF, "+default.e.l.f", "VBOX_LOG",
                                      RT_ELEMENTS(s_apszGroups), &s_apszGroups[0], RTLOGDEST_USER /* backdoor */, NULL);
                AssertRC(rc2);
                if (RT_SUCCESS(rc2))
                {
                    RTLogSetDefaultInstance(pLogger);
                    RTLogRelSetDefaultInstance(pLogger);
                }

                LogFlow(("VBoxDX: Built %s %s", __DATE__, __TIME__));
                return TRUE;
            }

#ifdef DEBUG
            vboxVDbgVEHandlerUnregister();
#endif
            break;
        }

        case DLL_PROCESS_DETACH:
        {
            LogFlow(("VBoxDX: DLL_PROCESS_DETACH"));
            /// @todo RTR3Term();
#ifdef DEBUG
            vboxVDbgVEHandlerUnregister();
#endif
            return TRUE;
        }

        default:
            return TRUE;
    }
    return FALSE;
}
