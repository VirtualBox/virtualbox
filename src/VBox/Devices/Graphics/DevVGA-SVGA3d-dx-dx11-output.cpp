/* $Id: DevVGA-SVGA3d-dx-dx11-output.cpp 114064 2026-05-04 16:56:20Z vitali.pelenjow@oracle.com $ */
/** @file
 * DevSVGA - D3D11 backend graphics output utilities
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_DEV_VMSVGA

#include <VBox/log.h>

#include <iprt/mem.h>
#include <iprt/string.h>

#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
#endif

#include "DevVGA-SVGA3d-dx-dx11.h"

/* Output target transforms entire ScreenTexture and copies the result to a memory buffer.
 *
 * Steps:
 *   * Render (optional): screen texture -> output texture(s)
 *   * Copy (optional): output texture(s) -> staging texture(s)
 *   * Query: wait on a query for completion
 *   * Readback (optional): staging texture(s) -> memory buffers(s)
 *
 */

typedef struct DXOUTPUTTARGETMETHODS
{
    DECLR3CALLBACKMEMBER(void, pfnDXOutputTargetDestroy,(VMSVGAOUTPUTTARGET *pOutputTarget));
    DECLR3CALLBACKMEMBER(int, pfnDXOutputTargetConvert,(VMSVGAOUTPUTTARGET *pOutputTarget,
                                                        ID3D11DeviceContext1 *pDeviceContext,
                                                        ID3D11ShaderResourceView *pSrcSrv,
                                                        UINT srcW, UINT srcH,
                                                        SVGASignedRect const &dirtyRect));
    DECLR3CALLBACKMEMBER(int, pfnDXOutputTargetReadback,(VMSVGAOUTPUTTARGET *pOutputTarget,
                                                         ID3D11DeviceContext1 *pDeviceContext,
                                                         SVGASignedRect const &updateRect,
                                                         bool *pfChanged));
} DXOUTPUTTARGETMETHODS;

/* Generic OT object with target specific data and virtual methods. */
typedef struct VMSVGAHWOUTPUTTARGET
{
    DXOUTPUTTARGETMETHODS       methods;
} VMSVGAHWOUTPUTTARGET;

typedef struct DXOUTPUTTARGET_B8G8R8X8_I
{
    VMSVGAHWOUTPUTTARGET        Base;

    ID3D11Texture2D            *pStagingTexture;
} DXOUTPUTTARGET_B8G8R8X8_I;

typedef struct DXTARGET2DUAV
{
    ID3D11Texture2D            *pT2D;
    ID3D11UnorderedAccessView  *pUAV;
    ID3D11Texture2D            *pT2DStaging;
} DXTARGET2DUAV;

typedef struct DXOUTPUTTARGET_I420
{
    VMSVGAHWOUTPUTTARGET        Base;

    ID3D11ComputeShader        *pCSy;
    ID3D11ComputeShader        *pCSuv;

    ID3D11Buffer               *pCSConstantBuffer;
    ID3D11SamplerState         *pSamplerState;

    DXGI_FORMAT                 enmPlaneFormat;

    DXTARGET2DUAV               y;
    DXTARGET2DUAV               u;
    DXTARGET2DUAV               v;
} DXOUTPUTTARGET_I420;


static DECLCALLBACK(int) dxOutputTargetCreate_B8G8R8X8_I(VMSVGAOUTPUTTARGET *pOutputTarget,
                                                         ID3D11Device1 *pDevice)
{
    DXOUTPUTTARGET_B8G8R8X8_I *pThis = (DXOUTPUTTARGET_B8G8R8X8_I *)pOutputTarget->pHwOutputTarget;

    /* Staging texture for downloading the screen content to the system memory. */
    D3D11_TEXTURE2D_DESC td;
    RT_ZERO(td);
    td.Width              = pOutputTarget->desc.cWidth;
    td.Height             = pOutputTarget->desc.cHeight;
    td.MipLevels          = 1;
    td.ArraySize          = 1;
    td.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count   = 1;
    //td.SampleDesc.Quality = 0;
    td.Usage              = D3D11_USAGE_STAGING;
    //td.BindFlags          = 0;
    td.CPUAccessFlags     = D3D11_CPU_ACCESS_READ;
    //td.MiscFlags          = 0;

    HRESULT hr = pDevice->CreateTexture2D(&td, 0, &pThis->pStagingTexture);
    AssertReturn(SUCCEEDED(hr), VERR_NO_MEMORY);

    return VINF_SUCCESS;
}


static DECLCALLBACK(void) dxOutputTargetDestroy_B8G8R8X8_I(VMSVGAOUTPUTTARGET *pOutputTarget)
{
    DXOUTPUTTARGET_B8G8R8X8_I *pThis = (DXOUTPUTTARGET_B8G8R8X8_I *)pOutputTarget->pHwOutputTarget;
    D3D_RELEASE(pThis->pStagingTexture);
}


static DECLCALLBACK(int) dxOutputTargetConvert_B8G8R8X8_I(VMSVGAOUTPUTTARGET *pOutputTarget,
                                                          ID3D11DeviceContext1 *pDeviceContext,
                                                          ID3D11ShaderResourceView *pSrcSrv,
                                                          UINT srcW, UINT srcH,
                                                          SVGASignedRect const &dirtyRect)
{
    DXOUTPUTTARGET_B8G8R8X8_I *pThis = (DXOUTPUTTARGET_B8G8R8X8_I *)pOutputTarget->pHwOutputTarget;

    SVGASignedRect clipRect = dirtyRect;
    SVGASignedRect const boundRect = { 0, 0, (int32_t)srcW, (int32_t)srcH };
    vmsvgaR3ClipRect(&boundRect, &clipRect);
    if (clipRect.left >= clipRect.right || clipRect.top >= clipRect.bottom)
        return VINF_SUCCESS;

    D3D11_BOX srcBox;
    srcBox.left   = (UINT)clipRect.left;
    srcBox.top    = (UINT)clipRect.top;
    srcBox.front  = 0;
    srcBox.right  = (UINT)clipRect.right;
    srcBox.bottom = (UINT)clipRect.bottom;
    srcBox.back   = 1;

    ID3D11Resource *pSrcResource = NULL;
    pSrcSrv->GetResource(&pSrcResource);
    pDeviceContext->CopySubresourceRegion(pThis->pStagingTexture, 0,
                                          (UINT)clipRect.left, (UINT)clipRect.top, 0,
                                          pSrcResource, 0, &srcBox);
    D3D_RELEASE(pSrcResource);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) dxOutputTargetReadback_B8G8R8X8_I(VMSVGAOUTPUTTARGET *pOutputTarget,
                                                           ID3D11DeviceContext1 *pDeviceContext,
                                                           SVGASignedRect const &updateRect,
                                                           bool *pfChanged)
{
    DXOUTPUTTARGET_B8G8R8X8_I *pThis = (DXOUTPUTTARGET_B8G8R8X8_I *)pOutputTarget->pHwOutputTarget;
    *pfChanged = false;

    /* Copy data from staging resource to the system memory. */
    D3D11_MAPPED_SUBRESOURCE map;
    RT_ZERO(map);

    HRESULT hr = pDeviceContext->Map(pThis->pStagingTexture, 0, D3D11_MAP_READ, 0, &map);
    AssertReturn(SUCCEEDED(hr), VERR_NOT_SUPPORTED);

    uint32_t const cWidth  = pOutputTarget->desc.cWidth;
    uint32_t const cHeight = pOutputTarget->desc.cHeight;
    uint32_t const cbPixel = 4;

    uint8_t *pu8Dst = (uint8_t *)pOutputTarget->desc.pvOutputBuffer;
    uint8_t const *pu8Src = (uint8_t *)map.pData;

    if (   updateRect.left == 0
        && updateRect.top == 0
        && updateRect.right == (int32_t)cWidth
        && updateRect.bottom == (int32_t)cHeight
        && cWidth * cbPixel == map.RowPitch)
    {
        size_t const cb = (size_t)map.RowPitch * cHeight;
        if (memcmp(pu8Dst, pu8Src, cb) != 0)
        {
            memcpy(pu8Dst, pu8Src, cb);
            *pfChanged = true;
        }
    }
    else
    {
        /* 'pu8Src' points to the entire mapped resource. Take the clipping box into account. */
        uint8_t const *pu8SrcBox = pu8Src
            + updateRect.left * cbPixel + updateRect.top * map.RowPitch;

        uint8_t *pu8DstBox = pu8Dst
            + updateRect.left * cbPixel + updateRect.top * cbPixel * cWidth;

        uint32_t const cbRowWidth = cbPixel * (updateRect.right - updateRect.left);
        for (int32_t iRow = 0; iRow < updateRect.bottom - updateRect.top; ++iRow)
        {
            if (memcmp(pu8DstBox, pu8SrcBox, cbRowWidth) != 0)
            {
                memcpy(pu8DstBox, pu8SrcBox, cbRowWidth);
                *pfChanged = true;
            }

            pu8SrcBox += map.RowPitch;
            pu8DstBox += cbPixel * cWidth;
        }
    }

    pDeviceContext->Unmap(pThis->pStagingTexture, 0);

    return VINF_SUCCESS;
}


#include "shaders/d3d11yuv.hlsl.cs_y.h"
#include "shaders/d3d11yuv.hlsl.cs_uv.h"

/* Compute shader parameters for YUV conversion. */
struct CSParameters
{
    /* Destination Y plane dimensions. */
    UINT dstW;
    UINT dstH;
    UINT pad0;
    UINT pad1;

    /* Offset of the scaled input image in the Y plane in output pixels */
    float dstOffX;
    float dstOffY;
    /* 1/scaledW, 1/scaledH */
    float invScaledW;
    float invScaledH;
};


static void computeCSParameters(struct CSParameters *p, UINT srcW, UINT srcH, UINT dstW, UINT dstH)
{
    p->dstW = dstW;
    p->dstH = dstH;

    float scale;
    if (srcW <= dstW && srcH <= dstH)
        scale = 1.0f;
    else
    {
        float const scaleX = (float)dstW / (float)srcW;
        float const scaleY = (float)dstH / (float)srcH;
        scale = RT_MIN(scaleX, scaleY);
    }

    float const scaledW = (float)srcW * scale;
    float const scaledH = (float)srcH * scale;

    p->dstOffX = 0.5f * ((float)dstW - scaledW);
    p->dstOffY = 0.5f * ((float)dstH - scaledH);

    p->invScaledW = 1.0f / scaledW;
    p->invScaledH = 1.0f / scaledH;
}


static void dxTarget2DUAVCleanup(DXTARGET2DUAV *p)
{
    D3D_RELEASE(p->pT2DStaging);
    D3D_RELEASE(p->pUAV);
    D3D_RELEASE(p->pT2D);
}


static void dxOutputTargetCleanup(DXOUTPUTTARGET_I420 *pHwOutputTarget)
{
    dxTarget2DUAVCleanup(&pHwOutputTarget->v);
    dxTarget2DUAVCleanup(&pHwOutputTarget->u);
    dxTarget2DUAVCleanup(&pHwOutputTarget->y);

    D3D_RELEASE(pHwOutputTarget->pSamplerState);
    D3D_RELEASE(pHwOutputTarget->pCSConstantBuffer);
    D3D_RELEASE(pHwOutputTarget->pCSuv);
    D3D_RELEASE(pHwOutputTarget->pCSy);
}


static bool dxFormatSupportsTypedUAV(ID3D11Device1 *pDevice, DXGI_FORMAT dxgiFormat)
{
    UINT FormatSupport = 0;
    if (FAILED(pDevice->CheckFormatSupport(dxgiFormat, &FormatSupport)))
        return false;

    return RT_BOOL(FormatSupport & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW);
}


static DXGI_FORMAT dxChooseYUVPlaneFormat(ID3D11Device1 *pDevice)
{
    if (dxFormatSupportsTypedUAV(pDevice, DXGI_FORMAT_R8_UNORM))
        return DXGI_FORMAT_R8_UNORM;

    if (dxFormatSupportsTypedUAV(pDevice, DXGI_FORMAT_R16_UNORM))
        return DXGI_FORMAT_R16_UNORM;

    return DXGI_FORMAT_UNKNOWN;
}


static HRESULT dxTarget2DUAVCreate(ID3D11Device1 *pDevice,
                                   uint32_t cWidth,
                                   uint32_t cHeight,
                                   DXGI_FORMAT dxgiFormat,
                                   DXTARGET2DUAV *pPlane)
{
    D3D11_TEXTURE2D_DESC texDesc;
    RT_ZERO(texDesc);
    texDesc.Width            = cWidth;
    texDesc.Height           = cHeight;
    texDesc.MipLevels        = 1;
    texDesc.ArraySize        = 1;
    texDesc.Format           = dxgiFormat;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage            = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags        = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = pDevice->CreateTexture2D(&texDesc, NULL, &pPlane->pT2D);
    AssertReturn(SUCCEEDED(hr), hr);

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
    RT_ZERO(uavDesc);
    uavDesc.Format             = texDesc.Format;
    uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;

    hr = pDevice->CreateUnorderedAccessView(pPlane->pT2D, &uavDesc, &pPlane->pUAV);
    AssertReturn(SUCCEEDED(hr), hr);

    texDesc.Usage          = D3D11_USAGE_STAGING;
    texDesc.BindFlags      = 0;
    texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    hr = pDevice->CreateTexture2D(&texDesc, NULL, &pPlane->pT2DStaging);
    AssertReturn(SUCCEEDED(hr), hr);

    return S_OK;
}


static DECLCALLBACK(int) dxOutputTargetCreate_I420(VMSVGAOUTPUTTARGET *pOutputTarget,
                                                   ID3D11Device1 *pDevice)
{
    DXOUTPUTTARGET_I420 *pHwOutputTarget = (DXOUTPUTTARGET_I420 *)pOutputTarget->pHwOutputTarget;

    uint32_t const cWidth = pOutputTarget->desc.cWidth;
    uint32_t const cHeight = pOutputTarget->desc.cHeight;

    /* Texture dimensions must be a multiple of 2. */
    AssertReturn(cWidth > 0 && cHeight > 0, VERR_INVALID_PARAMETER);
    AssertReturn(   cWidth <= D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
                 && cHeight <= D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION, VERR_INVALID_PARAMETER);
    AssertReturn((cWidth & 1) == 0 && (cHeight & 1) == 0, VERR_INVALID_PARAMETER);

    /* R8_UNORM (preferrable) or R16_UNORM. */
    DXGI_FORMAT const dxgiFormatPlane = dxChooseYUVPlaneFormat(pDevice);
    AssertReturn(dxgiFormatPlane != DXGI_FORMAT_UNKNOWN, VERR_NOT_SUPPORTED);

    pHwOutputTarget->enmPlaneFormat  = dxgiFormatPlane;

    /* Compute shaders. */
    HRESULT hr = pDevice->CreateComputeShader(g_cs_y, sizeof(g_cs_y), NULL, &pHwOutputTarget->pCSy);
    AssertReturn(SUCCEEDED(hr), VERR_NO_MEMORY);
    hr = pDevice->CreateComputeShader(g_cs_uv, sizeof(g_cs_uv), NULL, &pHwOutputTarget->pCSuv);
    AssertReturn(SUCCEEDED(hr), VERR_NO_MEMORY);

    /* Constant buffer for compute shaders. */
    D3D11_BUFFER_DESC constantBufferDesc;
    RT_ZERO(constantBufferDesc);
    constantBufferDesc.ByteWidth      = RT_ALIGN_32(sizeof(CSParameters), 16);
    constantBufferDesc.Usage          = D3D11_USAGE_DYNAMIC;
    constantBufferDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    constantBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = pDevice->CreateBuffer(&constantBufferDesc, NULL, &pHwOutputTarget->pCSConstantBuffer);
    AssertReturn(SUCCEEDED(hr), VERR_NO_MEMORY);

    /* Linear clamp sampler for scaling. */
    D3D11_SAMPLER_DESC samplerDesc;
    RT_ZERO(samplerDesc);
    samplerDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxAnisotropy  = 1;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;

    hr = pDevice->CreateSamplerState(&samplerDesc, &pHwOutputTarget->pSamplerState);
    AssertReturn(SUCCEEDED(hr), VERR_NO_MEMORY);

    /* Textures for Y, U, V planes. */
    hr = dxTarget2DUAVCreate(pDevice, cWidth, cHeight, dxgiFormatPlane, &pHwOutputTarget->y);
    AssertReturn(SUCCEEDED(hr), VERR_NO_MEMORY);
    hr = dxTarget2DUAVCreate(pDevice, cWidth / 2, cHeight / 2, dxgiFormatPlane, &pHwOutputTarget->u);
    AssertReturn(SUCCEEDED(hr), VERR_NO_MEMORY);
    hr = dxTarget2DUAVCreate(pDevice, cWidth / 2, cHeight / 2, dxgiFormatPlane, &pHwOutputTarget->v);
    AssertReturn(SUCCEEDED(hr), VERR_NO_MEMORY);

    return VINF_SUCCESS;
}


static DECLCALLBACK(void) dxOutputTargetDestroy_I420(VMSVGAOUTPUTTARGET *pOutputTarget)
{
    DXOUTPUTTARGET_I420 *pHwOutputTarget = (DXOUTPUTTARGET_I420 *)pOutputTarget->pHwOutputTarget;
    dxOutputTargetCleanup(pHwOutputTarget);
}


static DECLCALLBACK(int) dxOutputTargetConvert_I420(VMSVGAOUTPUTTARGET *pOutputTarget,
                                                    ID3D11DeviceContext1 *pDeviceContext,
                                                    ID3D11ShaderResourceView *pSrcSrv,
                                                    UINT srcW, UINT srcH,
                                                    SVGASignedRect const &dirtyRect)
{
    RT_NOREF(dirtyRect);

    DXOUTPUTTARGET_I420 *pHwOutputTarget = (DXOUTPUTTARGET_I420 *)pOutputTarget->pHwOutputTarget;

    /* Save/restore pipeline state.
     * Shader, shader resource and UAVs are set by setupPipeline.
     */
    /** @todo Update after state tracking redesign. */
    ID3D11Buffer *pSavedConstantBuffer;
    pDeviceContext->CSGetConstantBuffers(0, 1, &pSavedConstantBuffer);
    ID3D11SamplerState *pSavedSamplerState;
    pDeviceContext->CSGetSamplers(0, 1, &pSavedSamplerState);

    /* Update compute shader parameters. */
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = pDeviceContext->Map(pHwOutputTarget->pCSConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    AssertReturn(SUCCEEDED(hr), VERR_INTERNAL_ERROR);

    CSParameters *pParams = (CSParameters *)mapped.pData;
    computeCSParameters(pParams, srcW, srcH, pOutputTarget->desc.cWidth, pOutputTarget->desc.cHeight);

    pDeviceContext->Unmap(pHwOutputTarget->pCSConstantBuffer, 0);

    /* Invoke compute shaders to convert source to destination planes. */
    ID3D11UnorderedAccessView *apUav[2] = { NULL, NULL };
    UINT uInitialCounts[2] = { 0, 0 };

    /* Dispatch the luminance pass. */
    pDeviceContext->CSSetShader(pHwOutputTarget->pCSy, NULL, 0);
    pDeviceContext->CSSetConstantBuffers(0, 1, &pHwOutputTarget->pCSConstantBuffer);
    pDeviceContext->CSSetShaderResources(0, 1, &pSrcSrv);
    pDeviceContext->CSSetSamplers(0, 1, &pHwOutputTarget->pSamplerState);
    apUav[0] = pHwOutputTarget->y.pUAV;
    pDeviceContext->CSSetUnorderedAccessViews(0, 1, apUav, uInitialCounts);

    UINT const cGroupXy = (pOutputTarget->desc.cWidth + 15) / 16;
    UINT const cGroupYy = (pOutputTarget->desc.cHeight + 15) / 16;
    pDeviceContext->Dispatch(cGroupXy, cGroupYy, 1);

    /* Unbind UAV before reusing the slot (seems to be a good practice). */
    apUav[0] = NULL;
    pDeviceContext->CSSetUnorderedAccessViews(0, 1, apUav, uInitialCounts);

    /* Dispatch the chroma pass. */
    pDeviceContext->CSSetShader(pHwOutputTarget->pCSuv, NULL, 0);
    //pDeviceContext->CSSetConstantBuffers(0, 1, &pHwOutputTarget->pCSConstantBuffer);
    //pDeviceContext->CSSetShaderResources(0, 1, &pSrcSrv);
    pDeviceContext->CSSetSamplers(0, 1, &pHwOutputTarget->pSamplerState);
    apUav[0] = pHwOutputTarget->u.pUAV;
    apUav[1] = pHwOutputTarget->v.pUAV;
    pDeviceContext->CSSetUnorderedAccessViews(0, 2, apUav, NULL);

    /* U/V pass operates on half resolution. */
    UINT const cGroupXuv = ((pOutputTarget->desc.cWidth / 2) + 15) / 16;
    UINT const cGroupYuv = ((pOutputTarget->desc.cHeight / 2) + 15) / 16;
    pDeviceContext->Dispatch(cGroupXuv, cGroupYuv, 1);

    /* Unbind all compute shader state. */
    pDeviceContext->CSSetShader(NULL, NULL, 0);
    ID3D11Buffer *apNullBuffer[] = { NULL };
    pDeviceContext->CSSetConstantBuffers(0, RT_ELEMENTS(apNullBuffer), apNullBuffer);
    ID3D11ShaderResourceView *apNullSrv[] = { NULL };
    pDeviceContext->CSSetShaderResources(0, RT_ELEMENTS(apNullSrv), apNullSrv);
    ID3D11SamplerState *apNullSampler[] = { NULL };
    pDeviceContext->CSSetSamplers(0, RT_ELEMENTS(apNullSampler), apNullSampler);
    apUav[0] = NULL;
    apUav[1] = NULL;
    pDeviceContext->CSSetUnorderedAccessViews(0, 2, apUav, uInitialCounts);

    pDeviceContext->CSSetConstantBuffers(0, 1, &pSavedConstantBuffer);
    D3D_RELEASE(pSavedConstantBuffer);
    pDeviceContext->CSSetSamplers(0, 1, &pSavedSamplerState);
    D3D_RELEASE(pSavedSamplerState);

    /* Copy results into staging textures for CPU read-back. */
    pDeviceContext->CopyResource(pHwOutputTarget->y.pT2DStaging, pHwOutputTarget->y.pT2D);
    pDeviceContext->CopyResource(pHwOutputTarget->u.pT2DStaging, pHwOutputTarget->u.pT2D);
    pDeviceContext->CopyResource(pHwOutputTarget->v.pT2DStaging, pHwOutputTarget->v.pT2D);

    return VINF_SUCCESS;
}


static int dxCopyPlane(ID3D11DeviceContext1 *pDeviceContext,
                       ID3D11Texture2D *pStagingTexture,
                       uint32_t cWidth,
                       uint32_t cHeight,
                       DXGI_FORMAT enmPlaneFormat,
                       uint8_t *pu8Dst)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = pDeviceContext->Map(pStagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
    AssertReturn(SUCCEEDED(hr), VERR_NOT_SUPPORTED);

    uint8_t const *pu8Src = (uint8_t const *)mapped.pData;

    if (enmPlaneFormat == DXGI_FORMAT_R8_UNORM)
    {
        for (uint32_t y = 0; y < cHeight; ++y)
        {
            memcpy(pu8Dst, pu8Src, cWidth);

            pu8Dst += cWidth;
            pu8Src += mapped.RowPitch;
        }
    }
    else if (enmPlaneFormat == DXGI_FORMAT_R16_UNORM)
    {
        for (uint32_t y = 0; y < cHeight; ++y)
        {
            uint16_t const *pu16Src = (uint16_t const *)pu8Src;

            for (uint32_t x = 0; x < cWidth; ++x)
            {
                uint32_t const u32 = pu16Src[x]; /* Use uint32_t to avoid a theoretical overflow from +128. */
                pu8Dst[x] = (uint8_t)((u32 + 128) >> 8);
            }

            pu8Dst += cWidth;
            pu8Src += mapped.RowPitch;
        }
    }
    else
        AssertFailed();

    pDeviceContext->Unmap(pStagingTexture, 0);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) dxOutputTargetReadback_I420(VMSVGAOUTPUTTARGET *pOutputTarget,
                                                     ID3D11DeviceContext1 *pDeviceContext,
                                                     SVGASignedRect const &updateRect,
                                                     bool *pfChanged)
{
    RT_NOREF(updateRect);
    *pfChanged = true;

    DXOUTPUTTARGET_I420 *pHwOutputTarget = (DXOUTPUTTARGET_I420 *)pOutputTarget->pHwOutputTarget;

    AssertReturn(pOutputTarget->desc.enmFormat == PDMDISPLAYOUTPUTTARGETFORMAT_YUVI420,
                 VERR_INVALID_STATE);
    AssertReturn(   pHwOutputTarget->enmPlaneFormat == DXGI_FORMAT_R8_UNORM
                 || pHwOutputTarget->enmPlaneFormat == DXGI_FORMAT_R16_UNORM,
                 VERR_NOT_SUPPORTED);

    uint32_t const cWidthY  = pOutputTarget->desc.cWidth;
    uint32_t const cHeightY = pOutputTarget->desc.cHeight;
    uint32_t const cWidthUV  = cWidthY  / 2;
    uint32_t const cHeightUV = cHeightY / 2;

    size_t const cbY = (size_t)cWidthY * cHeightY;
    size_t const cbUV = (size_t)cWidthUV * cHeightUV;

    uint8_t *pu8DstY = (uint8_t *)pOutputTarget->desc.pvOutputBuffer;
    uint8_t *pu8DstU = pu8DstY + cbY;
    uint8_t *pu8DstV = pu8DstU + cbUV;

    int rc = dxCopyPlane(pDeviceContext, pHwOutputTarget->y.pT2DStaging,
                         cWidthY, cHeightY, pHwOutputTarget->enmPlaneFormat, pu8DstY);
    if (RT_SUCCESS(rc))
        rc = dxCopyPlane(pDeviceContext, pHwOutputTarget->u.pT2DStaging,
                         cWidthUV, cHeightUV, pHwOutputTarget->enmPlaneFormat, pu8DstU);
    if (RT_SUCCESS(rc))
        rc = dxCopyPlane(pDeviceContext, pHwOutputTarget->v.pT2DStaging,
                         cWidthUV, cHeightUV, pHwOutputTarget->enmPlaneFormat, pu8DstV);
    return rc;
}


typedef DECLCALLBACKTYPE(int, FNDXOUTPUTTARGETCREATE,(VMSVGAOUTPUTTARGET *pOutputTarget, ID3D11Device1 *pDevice));
typedef FNDXOUTPUTTARGETCREATE *PFNDXOUTPUTTARGETCREATE;

typedef struct DXOUTPUTTARGETDESC
{
    size_t                  cbDXOutputTarget;
    PFNDXOUTPUTTARGETCREATE pfnDXOutputTargetCreate;
    DXOUTPUTTARGETMETHODS   methods;
} DXOUTPUTTARGETDESC;


static DXOUTPUTTARGETDESC const desc_B8G8R8X8_I =
{
    sizeof(DXOUTPUTTARGET_B8G8R8X8_I),
    dxOutputTargetCreate_B8G8R8X8_I,
    {
        dxOutputTargetDestroy_B8G8R8X8_I,
        dxOutputTargetConvert_B8G8R8X8_I,
        dxOutputTargetReadback_B8G8R8X8_I
    }
};


static DXOUTPUTTARGETDESC const desc_I420 =
{
    sizeof(DXOUTPUTTARGET_I420),
    dxOutputTargetCreate_I420,
    {
        dxOutputTargetDestroy_I420,
        dxOutputTargetConvert_I420,
        dxOutputTargetReadback_I420
    }
};


int dxHwOutputTargetCreate(VMSVGAOUTPUTTARGET *pOutputTarget,
                           ID3D11Device1 *pDevice)
{
    Assert(pOutputTarget->pHwOutputTarget == NULL);

    DXOUTPUTTARGETDESC const *pDesc = NULL;

    switch (pOutputTarget->desc.enmFormat)
    {
        case PDMDISPLAYOUTPUTTARGETFORMAT_B8G8R8X8_I:
            pDesc = &desc_B8G8R8X8_I;
            break;

        case PDMDISPLAYOUTPUTTARGETFORMAT_YUVI420:
            pDesc = &desc_I420;
            break;

        default:
            break;
    }

    if (pDesc == NULL)
        return VERR_NOT_IMPLEMENTED;

    VMSVGAHWOUTPUTTARGET *pHwOutputTarget = (VMSVGAHWOUTPUTTARGET *)RTMemAllocZ(pDesc->cbDXOutputTarget);
    AssertPtrReturn(pHwOutputTarget, VERR_NO_MEMORY);

    /* The caller will do a cleanup on failure. */
    pOutputTarget->pHwOutputTarget = pHwOutputTarget;

    pHwOutputTarget->methods = pDesc->methods;

    return pDesc->pfnDXOutputTargetCreate(pOutputTarget, pDevice);
}


void dxHwOutputTargetDestroy(VMSVGAOUTPUTTARGET *pOutputTarget)
{
    AssertReturnVoid(pOutputTarget);

    VMSVGAHWOUTPUTTARGET *pHwOutputTarget = pOutputTarget->pHwOutputTarget;
    if (!pHwOutputTarget)
        return;

    if (pHwOutputTarget->methods.pfnDXOutputTargetDestroy)
        pHwOutputTarget->methods.pfnDXOutputTargetDestroy(pOutputTarget);

    RTMemFree(pOutputTarget->pHwOutputTarget);
    pOutputTarget->pHwOutputTarget = NULL;
}


int dxHwOutputTargetConvert(VMSVGAOUTPUTTARGET *pOutputTarget,
                            ID3D11DeviceContext1 *pDeviceContext,
                            ID3D11ShaderResourceView *pSrcSrv,
                            UINT srcW, UINT srcH,
                            SVGASignedRect const &dirtyRect)
{
    VMSVGAHWOUTPUTTARGET *pHwOutputTarget = pOutputTarget->pHwOutputTarget;
    if (pHwOutputTarget->methods.pfnDXOutputTargetConvert)
        return pHwOutputTarget->methods.pfnDXOutputTargetConvert(pOutputTarget, pDeviceContext, pSrcSrv,
                                                                 srcW, srcH, dirtyRect);
    return VINF_SUCCESS;
}


int dxHwOutputTargetReadback(VMSVGAOUTPUTTARGET *pOutputTarget,
                             ID3D11DeviceContext1 *pDeviceContext,
                             SVGASignedRect const &updateRect,
                             bool *pfChanged)
{
    *pfChanged = false;

    SVGASignedRect clipRect = updateRect;
    SVGASignedRect const boundRect = { 0 , 0, (int32_t)pOutputTarget->desc.cWidth, (int32_t)pOutputTarget->desc.cHeight };
    vmsvgaR3ClipRect(&boundRect, &clipRect);
    if (clipRect.left >= clipRect.right || clipRect.top >= clipRect.bottom)
        return VINF_SUCCESS;

    VMSVGAHWOUTPUTTARGET *pHwOutputTarget = pOutputTarget->pHwOutputTarget;
    if (pHwOutputTarget->methods.pfnDXOutputTargetReadback)
        return pHwOutputTarget->methods.pfnDXOutputTargetReadback(pOutputTarget, pDeviceContext, clipRect, pfChanged);
    return VINF_SUCCESS;
}
