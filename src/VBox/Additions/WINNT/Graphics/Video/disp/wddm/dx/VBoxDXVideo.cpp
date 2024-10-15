/* $Id: VBoxDXVideo.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox D3D user mode driver.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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


static void vboxDXDestroyVideoDeviceAllocation(PVBOXDX_DEVICE pDevice)
{
    if (pDevice->VideoDevice.hAllocation)
    {
        D3DDDICB_DEALLOCATE ddiDeallocate;
        RT_ZERO(ddiDeallocate);
        ddiDeallocate.NumAllocations = 1;
        ddiDeallocate.HandleList     = &pDevice->VideoDevice.hAllocation;

        HRESULT hr = pDevice->pRTCallbacks->pfnDeallocateCb(pDevice->hRTDevice.handle, &ddiDeallocate);
        LogFlowFunc(("pfnDeallocateCb returned %d", hr));
        AssertStmt(SUCCEEDED(hr), vboxDXDeviceSetError(pDevice, hr));

        pDevice->VideoDevice.hAllocation = 0;
        pDevice->VideoDevice.cbAllocation = 0;
    }
}


static bool vboxDXCreateVideoDeviceAllocation(PVBOXDX_DEVICE pDevice, uint32_t cbAllocation)
{
    VBOXDXALLOCATIONDESC desc;
    RT_ZERO(desc);
    desc.enmAllocationType = VBOXDXALLOCATIONTYPE_CO; /* Context Object allocation. */
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
    AssertReturnStmt(SUCCEEDED(hr), vboxDXDeviceSetError(pDevice, hr), false);

    pDevice->VideoDevice.hAllocation = ddiAllocationInfo.hAllocation;
    pDevice->VideoDevice.cbAllocation = cbAllocation;

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
                     vboxDXDestroyVideoDeviceAllocation(pDevice); vboxDXDeviceSetError(pDevice, hr), false);
    return true;
}


static bool vboxDXEnsureVideoDeviceAllocation(PVBOXDX_DEVICE pDevice)
{
    if (!pDevice->VideoDevice.hAllocation)
        return vboxDXCreateVideoDeviceAllocation(pDevice, _64K);
    return true;
}


static void vboxDXQueryVideoCapability(PVBOXDX_DEVICE pDevice, VBSVGA3dVideoCapability cap,
                                       void const *pvDataIn, uint32_t cbDataIn,
                                       void **ppvDataOut, uint32_t *pcbDataOut)
{
    bool fSuccess = vboxDXEnsureVideoDeviceAllocation(pDevice);
    if (!fSuccess)
        return;

    uint32 const offsetInBytes = 0;
    uint32 const sizeInBytes = pDevice->VideoDevice.cbAllocation - offsetInBytes;
    AssertReturnVoid(cbDataIn <= sizeInBytes - RT_UOFFSETOF(VBSVGA3dVideoCapabilityMobLayout, data));

    if (cbDataIn)
    {
        D3DDDICB_LOCK ddiLock;
        RT_ZERO(ddiLock);
        ddiLock.hAllocation = pDevice->VideoDevice.hAllocation;
        ddiLock.Flags.ReadOnly = 1;
        HRESULT hr = pDevice->pRTCallbacks->pfnLockCb(pDevice->hRTDevice.handle, &ddiLock);
        if (SUCCEEDED(hr))
        {
            VBSVGA3dVideoCapabilityMobLayout *pCap = (VBSVGA3dVideoCapabilityMobLayout *)ddiLock.pData;
            memcpy(&pCap->data, pvDataIn, cbDataIn);

            D3DDDICB_UNLOCK ddiUnlock;
            ddiUnlock.NumAllocations = 1;
            ddiUnlock.phAllocations = &pDevice->VideoDevice.hAllocation;
            hr = pDevice->pRTCallbacks->pfnUnlockCb(pDevice->hRTDevice.handle, &ddiUnlock);
        }
        AssertReturnVoidStmt(SUCCEEDED(hr), vboxDXDeviceSetError(pDevice, hr));
    }

    uint64_t const fenceValue = ASMAtomicIncU64(&pDevice->VideoDevice.u64MobFenceValue);
    vgpu10GetVideoCapability(pDevice, cap, pDevice->VideoDevice.hAllocation, offsetInBytes, sizeInBytes, fenceValue);
    vboxDXDeviceFlushCommands(pDevice);

    /** @todo Time limit? */
    bool fDone = false;
    for (;;)
    {
        RTThreadYield();

        D3DDDICB_LOCK ddiLock;
        RT_ZERO(ddiLock);
        ddiLock.hAllocation = pDevice->VideoDevice.hAllocation;
        ddiLock.Flags.ReadOnly = 1;
        HRESULT hr = pDevice->pRTCallbacks->pfnLockCb(pDevice->hRTDevice.handle, &ddiLock);
        if (SUCCEEDED(hr))
        {
            VBSVGA3dVideoCapabilityMobLayout const *pCap = (VBSVGA3dVideoCapabilityMobLayout *)ddiLock.pData;
            if (pCap->fenceValue >= fenceValue)
            {
                if (pCap->cbDataOut > 0)
                {
                     void *pvData = RTMemAlloc(pCap->cbDataOut);
                     if (pvData)
                     {
                         memcpy(pvData, &pCap->data, pCap->cbDataOut);
                         *ppvDataOut = pvData;
                         *pcbDataOut = pCap->cbDataOut;
                     }
                     else
                         vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY);
                }

                fDone = true;
            }

            D3DDDICB_UNLOCK ddiUnlock;
            ddiUnlock.NumAllocations = 1;
            ddiUnlock.phAllocations = &pDevice->VideoDevice.hAllocation;
            hr = pDevice->pRTCallbacks->pfnUnlockCb(pDevice->hRTDevice.handle, &ddiUnlock);
        }
        AssertReturnVoidStmt(SUCCEEDED(hr), vboxDXDeviceSetError(pDevice, hr));

        if (fDone)
           break;
    }
}


static void vboxDXQueryVideoDecodeProfiles(PVBOXDX_DEVICE pDevice)
{
    RTMemFree(pDevice->VideoDevice.paDecodeProfile);
    pDevice->VideoDevice.cDecodeProfile = 0;

    /* On failure the pDevice->VideoDevice.cDecodeProfile value is 0, i.e. no decoding. */
    pDevice->VideoDevice.fDecodeProfilesQueried = true;

    void *pvDataOut = 0;
    uint32_t cbDataOut = 0;
    vboxDXQueryVideoCapability(pDevice, VBSVGA3D_VIDEO_CAPABILITY_DECODE_PROFILE,
                               NULL, 0, &pvDataOut, &cbDataOut);
    if (pvDataOut)
    {
        pDevice->VideoDevice.cDecodeProfile = cbDataOut / sizeof(VBSVGA3dDecodeProfileInfo);
        pDevice->VideoDevice.paDecodeProfile = (VBSVGA3dDecodeProfileInfo *)pvDataOut;
    }
}


static void vboxDXQueryVideoDecodeConfig(PVBOXDX_DEVICE pDevice, VBSVGA3dVideoDecoderDesc const &desc)
{
    RTMemFree(pDevice->VideoDevice.config.pConfigInfo);
    pDevice->VideoDevice.config.cConfig = 0;

    void *pvDataOut = 0;
    uint32_t cbDataOut = 0;
    vboxDXQueryVideoCapability(pDevice, VBSVGA3D_VIDEO_CAPABILITY_DECODE_CONFIG,
                               &desc, sizeof(desc), &pvDataOut, &cbDataOut);
    if (pvDataOut)
    {
        AssertReturnVoidStmt(cbDataOut >= sizeof(desc), RTMemFree(pvDataOut));

        VBSVGA3dDecodeConfigInfo *pConfigInfo = (VBSVGA3dDecodeConfigInfo *)pvDataOut;
        pDevice->VideoDevice.config.pConfigInfo = pConfigInfo;
        pDevice->VideoDevice.config.cConfig = (cbDataOut - sizeof(pConfigInfo->desc)) / sizeof(pConfigInfo->aConfig[0]);
    }
}


static void vboxDXQueryVideoProcessorEnumInfo(PVBOXDX_DEVICE pDevice, VBSVGA3dVideoProcessorDesc const &desc)
{
    void *pvDataOut = 0;
    uint32_t cbDataOut = 0;
    vboxDXQueryVideoCapability(pDevice, VBSVGA3D_VIDEO_CAPABILITY_PROCESSOR_ENUM,
                               &desc, sizeof(desc), &pvDataOut, &cbDataOut);
    if (pvDataOut)
    {
        AssertReturnVoidStmt(cbDataOut >= sizeof(VBSVGA3dProcessorEnumInfo), RTMemFree(pvDataOut));

        VBSVGA3dProcessorEnumInfo *pInfo = (VBSVGA3dProcessorEnumInfo *)pvDataOut;
        pDevice->VideoDevice.videoProcessorEnum.desc = desc;
        pDevice->VideoDevice.videoProcessorEnum.info = pInfo->info;

        RTMemFree(pvDataOut);
    }
}


void vboxDXGetVideoDecoderProfileCount(PVBOXDX_DEVICE pDevice, UINT *pDecodeProfileCount)
{
    if (!pDevice->VideoDevice.fDecodeProfilesQueried)
        vboxDXQueryVideoDecodeProfiles(pDevice);

    *pDecodeProfileCount = pDevice->VideoDevice.cDecodeProfile;
}


void vboxDXGetVideoDecoderProfile(PVBOXDX_DEVICE pDevice, UINT Index, GUID *pGuid)
{
    if (!pDevice->VideoDevice.fDecodeProfilesQueried)
        vboxDXQueryVideoDecodeProfiles(pDevice);

    *pGuid = *(GUID *)&pDevice->VideoDevice.paDecodeProfile[Index].DecodeProfile;
}


void vboxDXCheckVideoDecoderFormat(PVBOXDX_DEVICE pDevice, GUID const *pDecodeProfile, DXGI_FORMAT Format, BOOL *pSupported)
{
    if (!pDevice->VideoDevice.fDecodeProfilesQueried)
        vboxDXQueryVideoDecodeProfiles(pDevice);

    for (unsigned i = 0; i < pDevice->VideoDevice.cDecodeProfile; ++i)
    {
        VBSVGA3dDecodeProfileInfo const *s = &pDevice->VideoDevice.paDecodeProfile[i];
        if (memcmp(&s->DecodeProfile, pDecodeProfile, sizeof(s->DecodeProfile)) == 0)
        {
            switch (Format)
            {
                case DXGI_FORMAT_AYUV: *pSupported = RT_BOOL(s->fAYUV); break;
                case DXGI_FORMAT_NV12: *pSupported = RT_BOOL(s->fNV12); break;
                case DXGI_FORMAT_420_OPAQUE: *pSupported = RT_BOOL(s->fNV12); break;
                case DXGI_FORMAT_YUY2: *pSupported = RT_BOOL(s->fYUY2); break;
                default:  *pSupported = false;
            }
            break;
        }
    }
}


static void vboxDXVideoDecoderDescToSvga(VBSVGA3dVideoDecoderDesc *pSvgaDecoderDesc, D3D11_1DDI_VIDEO_DECODER_DESC const &Desc)
{
    memcpy(&pSvgaDecoderDesc->DecodeProfile, &Desc.Guid, sizeof(VBSVGA3dGuid));
    pSvgaDecoderDesc->SampleWidth = Desc.SampleWidth;
    pSvgaDecoderDesc->SampleHeight = Desc.SampleHeight;
    pSvgaDecoderDesc->OutputFormat = vboxDXDxgiToSvgaFormat(Desc.OutputFormat);
}


void vboxDXGetVideoDecoderConfigCount(PVBOXDX_DEVICE pDevice, D3D11_1DDI_VIDEO_DECODER_DESC const *pDecodeDesc, UINT *pConfigCount)
{
    VBSVGA3dVideoDecoderDesc svgaDesc;
    vboxDXVideoDecoderDescToSvga(&svgaDesc, *pDecodeDesc);

    if (   !pDevice->VideoDevice.config.pConfigInfo
        || memcmp(&svgaDesc, &pDevice->VideoDevice.config.pConfigInfo->desc, sizeof(VBSVGA3dVideoDecoderDesc)) != 0)
        vboxDXQueryVideoDecodeConfig(pDevice, svgaDesc);

    *pConfigCount = pDevice->VideoDevice.config.cConfig;
}


void vboxDXGetVideoDecoderConfig(PVBOXDX_DEVICE pDevice, D3D11_1DDI_VIDEO_DECODER_DESC const *pDecodeDesc, UINT Index,
                                 D3D11_1DDI_VIDEO_DECODER_CONFIG *pConfig)
{
    VBSVGA3dVideoDecoderDesc svgaDesc;
    vboxDXVideoDecoderDescToSvga(&svgaDesc, *pDecodeDesc);

    if (   !pDevice->VideoDevice.config.pConfigInfo
        || memcmp(&svgaDesc, &pDevice->VideoDevice.config.pConfigInfo->desc, sizeof(VBSVGA3dVideoDecoderDesc)) != 0)
        vboxDXQueryVideoDecodeConfig(pDevice, svgaDesc);

    AssertReturnVoidStmt(Index <= pDevice->VideoDevice.config.cConfig, vboxDXDeviceSetError(pDevice, E_INVALIDARG));

    VBSVGA3dVideoDecoderConfig const *s = &pDevice->VideoDevice.config.pConfigInfo->aConfig[Index];
    memcpy(&pConfig->guidConfigBitstreamEncryption, &s->guidConfigBitstreamEncryption, sizeof(GUID));
    memcpy(&pConfig->guidConfigMBcontrolEncryption, &s->guidConfigMBcontrolEncryption, sizeof(GUID));
    memcpy(&pConfig->guidConfigResidDiffEncryption, &s->guidConfigResidDiffEncryption, sizeof(GUID));
    pConfig->ConfigBitstreamRaw             = s->ConfigBitstreamRaw;
    pConfig->ConfigMBcontrolRasterOrder     = s->ConfigMBcontrolRasterOrder;
    pConfig->ConfigResidDiffHost            = s->ConfigResidDiffHost;
    pConfig->ConfigSpatialResid8            = s->ConfigSpatialResid8;
    pConfig->ConfigResid8Subtraction        = s->ConfigResid8Subtraction;
    pConfig->ConfigSpatialHost8or9Clipping  = s->ConfigSpatialHost8or9Clipping;
    pConfig->ConfigSpatialResidInterleaved  = s->ConfigSpatialResidInterleaved;
    pConfig->ConfigIntraResidUnsigned       = s->ConfigIntraResidUnsigned;
    pConfig->ConfigResidDiffAccelerator     = s->ConfigResidDiffAccelerator;
    pConfig->ConfigHostInverseScan          = s->ConfigHostInverseScan;
    pConfig->ConfigSpecificIDCT             = s->ConfigSpecificIDCT;
    pConfig->Config4GroupedCoefs            = s->Config4GroupedCoefs;
    pConfig->ConfigMinRenderTargetBuffCount = s->ConfigMinRenderTargetBuffCount;
    pConfig->ConfigDecoderSpecific          = s->ConfigDecoderSpecific;
}


HRESULT vboxDXCreateVideoProcessorEnum(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum,
                                       D3D11_1DDI_VIDEO_PROCESSOR_CONTENT_DESC const *pDesc)
{
    RT_NOREF(pDevice);

    pVideoProcessorEnum->svga.desc.InputFrameFormat = pDesc->InputFrameFormat;
    pVideoProcessorEnum->svga.desc.InputFrameRate.numerator = pDesc->InputFrameRate.Numerator;
    pVideoProcessorEnum->svga.desc.InputFrameRate.denominator = pDesc->InputFrameRate.Denominator;
    pVideoProcessorEnum->svga.desc.InputWidth       = pDesc->InputWidth;
    pVideoProcessorEnum->svga.desc.InputHeight      = pDesc->InputHeight;
    pVideoProcessorEnum->svga.desc.OutputFrameRate.numerator = pDesc->OutputFrameRate.Numerator;
    pVideoProcessorEnum->svga.desc.OutputFrameRate.denominator = pDesc->OutputFrameRate.Denominator;
    pVideoProcessorEnum->svga.desc.OutputWidth      = pDesc->OutputWidth;
    pVideoProcessorEnum->svga.desc.OutputHeight     = pDesc->OutputHeight;
    pVideoProcessorEnum->svga.desc.Usage            = pDesc->Usage;
    return S_OK;
}


void vboxDXCheckVideoProcessorFormat(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum,
                                     DXGI_FORMAT Format, UINT *pSupported)
{
    if (memcmp(&pVideoProcessorEnum->svga.desc,
               &pDevice->VideoDevice.videoProcessorEnum.desc, sizeof(pVideoProcessorEnum->svga.desc)) != 0)
        vboxDXQueryVideoProcessorEnumInfo(pDevice, pVideoProcessorEnum->svga.desc);

    AssertCompile(D3D11_1DDI_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT == VBSVGA3D_VP_FORMAT_SUPPORT_INPUT);
    AssertCompile(D3D11_1DDI_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT == VBSVGA3D_VP_FORMAT_SUPPORT_OUTPUT);
    VBSVGA3dVideoProcessorEnumInfo const *pInfo = &pDevice->VideoDevice.videoProcessorEnum.info;
    switch (Format)
    {
        case DXGI_FORMAT_R8_UNORM:            *pSupported = pInfo->fR8_UNORM; break;
        case DXGI_FORMAT_R16_UNORM:           *pSupported = pInfo->fR16_UNORM; break;
        case DXGI_FORMAT_NV12:                *pSupported = pInfo->fNV12; break;
        case DXGI_FORMAT_YUY2:                *pSupported = pInfo->fYUY2; break;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:  *pSupported = pInfo->fR16G16B16A16_FLOAT; break;
        case DXGI_FORMAT_B8G8R8X8_UNORM:      *pSupported = pInfo->fB8G8R8X8_UNORM; break;
        case DXGI_FORMAT_B8G8R8A8_UNORM:      *pSupported = pInfo->fB8G8R8A8_UNORM; break;
        case DXGI_FORMAT_R8G8B8A8_UNORM:      *pSupported = pInfo->fR8G8B8A8_UNORM; break;
        case DXGI_FORMAT_R10G10B10A2_UNORM:   *pSupported = pInfo->fR10G10B10A2_UNORM; break;
        case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM: *pSupported = pInfo->fR10G10B10_XR_BIAS_A2_UNORM; break;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: *pSupported = pInfo->fR8G8B8A8_UNORM_SRGB; break;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: *pSupported = pInfo->fB8G8R8A8_UNORM_SRGB; break;
        default:  *pSupported = 0;
    }
}


void vboxDXGetVideoProcessorCaps(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum,
                                 D3D11_1DDI_VIDEO_PROCESSOR_CAPS *pCaps)
{
    if (memcmp(&pVideoProcessorEnum->svga.desc,
               &pDevice->VideoDevice.videoProcessorEnum.desc, sizeof(pVideoProcessorEnum->svga.desc)) != 0)
        vboxDXQueryVideoProcessorEnumInfo(pDevice, pVideoProcessorEnum->svga.desc);

    VBSVGA3dVideoProcessorEnumInfo const *pInfo = &pDevice->VideoDevice.videoProcessorEnum.info;
    pCaps->DeviceCaps              = pInfo->Caps.DeviceCaps;
    pCaps->FeatureCaps             = pInfo->Caps.FeatureCaps;
    pCaps->FilterCaps              = pInfo->Caps.FilterCaps;
    pCaps->InputFormatCaps         = pInfo->Caps.InputFormatCaps;
    pCaps->AutoStreamCaps          = pInfo->Caps.AutoStreamCaps;
    pCaps->StereoCaps              = pInfo->Caps.StereoCaps;
    pCaps->RateConversionCapsCount = RT_MIN(pInfo->Caps.RateConversionCapsCount, VBSVGA3D_MAX_VIDEO_RATE_CONVERSION_CAPS);
    pCaps->MaxInputStreams         = RT_MIN(pInfo->Caps.MaxInputStreams, VBSVGA3D_MAX_VIDEO_STREAMS);
    pCaps->MaxStreamStates         = RT_MIN(pInfo->Caps.MaxStreamStates, VBSVGA3D_MAX_VIDEO_STREAMS);
}


void vboxDXGetVideoProcessorRateConversionCaps(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum,
                                               D3D11_1DDI_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS *pCaps)
{
    if (memcmp(&pVideoProcessorEnum->svga.desc,
               &pDevice->VideoDevice.videoProcessorEnum.desc, sizeof(pVideoProcessorEnum->svga.desc)) != 0)
        vboxDXQueryVideoProcessorEnumInfo(pDevice, pVideoProcessorEnum->svga.desc);

    VBSVGA3dVideoProcessorEnumInfo const *pInfo = &pDevice->VideoDevice.videoProcessorEnum.info;
    pCaps->PastFrames      = pInfo->RateCaps.PastFrames;
    pCaps->FutureFrames    = pInfo->RateCaps.FutureFrames;
    pCaps->ConversionCaps  = pInfo->RateCaps.ProcessorCaps;
    pCaps->ITelecineCaps   = pInfo->RateCaps.ITelecineCaps;
    pCaps->CustomRateCount = RT_MIN(pInfo->RateCaps.CustomRateCount, VBSVGA3D_MAX_VIDEO_CUSTOM_RATE_CAPS);;
}


void vboxDXGetVideoProcessorCustomRate(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum,
                                       UINT CustomRateIndex, D3D11_1DDI_VIDEO_PROCESSOR_CUSTOM_RATE *pRate)
{
    if (memcmp(&pVideoProcessorEnum->svga.desc,
               &pDevice->VideoDevice.videoProcessorEnum.desc, sizeof(pVideoProcessorEnum->svga.desc)) != 0)
        vboxDXQueryVideoProcessorEnumInfo(pDevice, pVideoProcessorEnum->svga.desc);

    VBSVGA3dVideoProcessorEnumInfo const *pInfo = &pDevice->VideoDevice.videoProcessorEnum.info;
    VBSVGA3dVideoProcessorCustomRateCaps const *pCustomRateCaps = &pInfo->aCustomRateCaps[CustomRateIndex];
    pRate->CustomRate.Numerator   = pCustomRateCaps->CustomRate.numerator;
    pRate->CustomRate.Denominator = pCustomRateCaps->CustomRate.denominator;
    pRate->OutputFrames           = pCustomRateCaps->OutputFrames;
    pRate->InputInterlaced        = RT_BOOL(pCustomRateCaps->InputInterlaced);
    pRate->InputFramesOrFields    = pCustomRateCaps->InputFramesOrFields;
}


void vboxDXGetVideoProcessorFilterRange(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum,
                                        D3D11_1DDI_VIDEO_PROCESSOR_FILTER Filter, D3D11_1DDI_VIDEO_PROCESSOR_FILTER_RANGE *pFilterRange)
{
    if (memcmp(&pVideoProcessorEnum->svga.desc,
               &pDevice->VideoDevice.videoProcessorEnum.desc, sizeof(pVideoProcessorEnum->svga.desc)) != 0)
        vboxDXQueryVideoProcessorEnumInfo(pDevice, pVideoProcessorEnum->svga.desc);

    VBSVGA3dVideoProcessorEnumInfo const *pInfo = &pDevice->VideoDevice.videoProcessorEnum.info;
    pFilterRange->Minimum = pInfo->aFilterRange[Filter].Minimum;
    pFilterRange->Maximum = pInfo->aFilterRange[Filter].Maximum;
    pFilterRange->Default = pInfo->aFilterRange[Filter].Default;
    pFilterRange->Multiplier = pInfo->aFilterRange[Filter].Multiplier;
}


HRESULT vboxDXCreateVideoProcessor(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor,
                                   PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum, UINT RateConversionCapsIndex)
{
    AssertReturnStmt(RateConversionCapsIndex < VBSVGA3D_MAX_VIDEO_RATE_CONVERSION_CAPS, vboxDXDeviceSetError(pDevice, E_INVALIDARG), E_INVALIDARG);

    int rc = RTHandleTableAlloc(pDevice->hHTVideoProcessor, pVideoProcessor, &pVideoProcessor->uVideoProcessorId);
    AssertRCReturnStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY), E_OUTOFMEMORY);

    pVideoProcessor->svga.desc = pVideoProcessorEnum->svga.desc;

    vgpu10DefineVideoProcessor(pDevice, pVideoProcessor->uVideoProcessorId,
                               pVideoProcessor->svga.desc);
    return S_OK;
}


HRESULT vboxDXCreateVideoDecoderOutputView(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEODECODEROUTPUTVIEW pVideoDecoderOutputView, PVBOXDX_RESOURCE pResource,
                                           GUID const &DecodeProfile, UINT MipSlice, UINT FirstArraySlice, UINT ArraySize)
{
    /* The host API does not have these parameters. */
    RT_NOREF(MipSlice, ArraySize);
    Assert(MipSlice == 0 && ArraySize == 1);

    int rc = RTHandleTableAlloc(pDevice->hHTVideoDecoderOutputView, pVideoDecoderOutputView, &pVideoDecoderOutputView->uVideoDecoderOutputViewId);
    AssertRCReturnStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY), E_OUTOFMEMORY);

    pVideoDecoderOutputView->pResource = pResource;

    VBSVGA3dVDOVDesc *pDesc = &pVideoDecoderOutputView->svga.desc;
    memcpy(&pDesc->DecodeProfile, &DecodeProfile, sizeof(pDesc->DecodeProfile));
    pDesc->ViewDimension = VBSVGA3D_VDOV_DIMENSION_TEXTURE2D;
    pDesc->Texture2D.ArraySlice = FirstArraySlice;

    vgpu10DefineVideoDecoderOutputView(pDevice, pVideoDecoderOutputView->uVideoDecoderOutputViewId,
                                       vboxDXGetAllocation(pVideoDecoderOutputView->pResource),
                                       pVideoDecoderOutputView->svga.desc);

    pVideoDecoderOutputView->fDefined = true;
    RTListAppend(&pVideoDecoderOutputView->pResource->listVDOV, &pVideoDecoderOutputView->nodeView);
    return S_OK;
}


HRESULT vboxDXCreateVideoDecoder(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEODECODER pVideoDecoder,
                                 D3D11_1DDI_VIDEO_DECODER_DESC const &Desc,
                                 D3D11_1DDI_VIDEO_DECODER_CONFIG const &Config)
{
    int rc = RTHandleTableAlloc(pDevice->hHTVideoDecoder, pVideoDecoder, &pVideoDecoder->uVideoDecoderId);
    AssertRCReturnStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY), E_OUTOFMEMORY);

    vboxDXVideoDecoderDescToSvga(&pVideoDecoder->svga.Desc, Desc);

    memcpy(&pVideoDecoder->svga.Config.guidConfigBitstreamEncryption, &Config.guidConfigBitstreamEncryption, sizeof(VBSVGA3dGuid));
    memcpy(&pVideoDecoder->svga.Config.guidConfigMBcontrolEncryption, &Config.guidConfigMBcontrolEncryption, sizeof(VBSVGA3dGuid));
    memcpy(&pVideoDecoder->svga.Config.guidConfigResidDiffEncryption, &Config.guidConfigResidDiffEncryption, sizeof(VBSVGA3dGuid));
    pVideoDecoder->svga.Config.ConfigBitstreamRaw             = Config.ConfigBitstreamRaw;
    pVideoDecoder->svga.Config.ConfigMBcontrolRasterOrder     = Config.ConfigMBcontrolRasterOrder;
    pVideoDecoder->svga.Config.ConfigResidDiffHost            = Config.ConfigResidDiffHost;
    pVideoDecoder->svga.Config.ConfigSpatialResid8            = Config.ConfigSpatialResid8;
    pVideoDecoder->svga.Config.ConfigResid8Subtraction        = Config.ConfigResid8Subtraction;
    pVideoDecoder->svga.Config.ConfigSpatialHost8or9Clipping  = Config.ConfigSpatialHost8or9Clipping;
    pVideoDecoder->svga.Config.ConfigSpatialResidInterleaved  = Config.ConfigSpatialResidInterleaved;
    pVideoDecoder->svga.Config.ConfigIntraResidUnsigned       = Config.ConfigIntraResidUnsigned;
    pVideoDecoder->svga.Config.ConfigResidDiffAccelerator     = Config.ConfigResidDiffAccelerator;
    pVideoDecoder->svga.Config.ConfigHostInverseScan          = Config.ConfigHostInverseScan;
    pVideoDecoder->svga.Config.ConfigSpecificIDCT             = Config.ConfigSpecificIDCT;
    pVideoDecoder->svga.Config.Config4GroupedCoefs            = Config.Config4GroupedCoefs;
    pVideoDecoder->svga.Config.ConfigMinRenderTargetBuffCount = Config.ConfigMinRenderTargetBuffCount;
    pVideoDecoder->svga.Config.ConfigDecoderSpecific          = Config.ConfigDecoderSpecific;

    vgpu10DefineVideoDecoder(pDevice, pVideoDecoder->uVideoDecoderId,
                             pVideoDecoder->svga.Desc, pVideoDecoder->svga.Config);
    return S_OK;
}


HRESULT vboxDXVideoDecoderBeginFrame(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEODECODER pVideoDecoder,
                                     PVBOXDXVIDEODECODEROUTPUTVIEW pVideoDecoderOutputView,
                                     void const *pContentKey, UINT ContentKeySize)
{
    RT_NOREF(pContentKey, ContentKeySize); /** @todo vgpu10VideoDecoderBeginFrame2 */

    vgpu10VideoDecoderBeginFrame(pDevice, pVideoDecoder->uVideoDecoderId,
                                 pVideoDecoderOutputView->uVideoDecoderOutputViewId);
    return S_OK;
}


HRESULT vboxDXVideoDecoderSubmitBuffers(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEODECODER pVideoDecoder,
                                        UINT BufferCount, D3D11_1DDI_VIDEO_DECODER_BUFFER_DESC const *pBufferDesc)
{
    void *pvTmpBuffer = RTMemTmpAlloc(BufferCount * (sizeof(D3DKMT_HANDLE) + sizeof(VBSVGA3dVideoDecoderBufferDesc)));
    if (!pvTmpBuffer)
        return E_OUTOFMEMORY;

    D3DKMT_HANDLE *pahAllocation = (D3DKMT_HANDLE *)pvTmpBuffer;
    VBSVGA3dVideoDecoderBufferDesc *paBD = (VBSVGA3dVideoDecoderBufferDesc *)((uint8_t *)pvTmpBuffer + BufferCount * sizeof(D3DKMT_HANDLE));

    for (UINT i = 0; i < BufferCount; ++i)
    {
        D3D11_1DDI_VIDEO_DECODER_BUFFER_DESC const *s = &pBufferDesc[i];

        PVBOXDX_RESOURCE pBuffer = (PVBOXDX_RESOURCE)s->hResource.pDrvPrivate;
        pahAllocation[i] = vboxDXGetAllocation(pBuffer);

        VBSVGA3dVideoDecoderBufferDesc *d = &paBD[i];
        d->sidBuffer      = SVGA3D_INVALID_ID;
        switch(s->BufferType)
        {
            default:
            case D3D11_1DDI_VIDEO_DECODER_BUFFER_UNKNOWN:
                 AssertFailedReturnStmt(RTMemTmpFree(pvTmpBuffer), E_INVALIDARG);
            case D3D11_1DDI_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS:
                d->bufferType = VBSVGA3D_VD_BUFFER_PICTURE_PARAMETERS; break;
            case D3D11_1DDI_VIDEO_DECODER_BUFFER_MACROBLOCK_CONTROL:
                d->bufferType = VBSVGA3D_VD_BUFFER_MACROBLOCK_CONTROL; break;
            case D3D11_1DDI_VIDEO_DECODER_BUFFER_RESIDUAL_DIFFERENCE:
                d->bufferType = VBSVGA3D_VD_BUFFER_RESIDUAL_DIFFERENCE; break;
            case D3D11_1DDI_VIDEO_DECODER_BUFFER_DEBLOCKING_CONTROL:
                d->bufferType = VBSVGA3D_VD_BUFFER_DEBLOCKING_CONTROL; break;
            case D3D11_1DDI_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX:
                d->bufferType = VBSVGA3D_VD_BUFFER_INVERSE_QUANTIZATION_MATRIX; break;
            case D3D11_1DDI_VIDEO_DECODER_BUFFER_SLICE_CONTROL:
                d->bufferType = VBSVGA3D_VD_BUFFER_SLICE_CONTROL; break;
            case D3D11_1DDI_VIDEO_DECODER_BUFFER_BITSTREAM:
                d->bufferType = VBSVGA3D_VD_BUFFER_BITSTREAM; break;
            case D3D11_1DDI_VIDEO_DECODER_BUFFER_MOTION_VECTOR:
                d->bufferType = VBSVGA3D_VD_BUFFER_MOTION_VECTOR; break;
            case D3D11_1DDI_VIDEO_DECODER_BUFFER_FILM_GRAIN:
                d->bufferType = VBSVGA3D_VD_BUFFER_FILM_GRAIN; break;
        }
        d->dataOffset     = s->DataOffset;
        d->dataSize       = s->DataSize;
        d->firstMBaddress = s->FirstMBaddress;
        d->numMBsInBuffer = s->NumMBsInBuffer;
    }

    vgpu10VideoDecoderSubmitBuffers(pDevice, pVideoDecoder->uVideoDecoderId, BufferCount, pahAllocation, paBD);
    RTMemTmpFree(pvTmpBuffer);
    return S_OK;
}


HRESULT vboxDXVideoDecoderEndFrame(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEODECODER pVideoDecoder)
{
    vgpu10VideoDecoderEndFrame(pDevice, pVideoDecoder->uVideoDecoderId);
    return S_OK;
}


HRESULT vboxDXCreateVideoProcessorInputView(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSORINPUTVIEW pVideoProcessorInputView,
                                            PVBOXDX_RESOURCE pResource, PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum,
                                            UINT FourCC, UINT MipSlice, UINT FirstArraySlice, UINT ArraySize)
{
    RT_NOREF(ArraySize);
    Assert(ArraySize == 1); /** @todo D3D11 API does use this. */

    int rc = RTHandleTableAlloc(pDevice->hHTVideoProcessorInputView, pVideoProcessorInputView, &pVideoProcessorInputView->uVideoProcessorInputViewId);
    AssertRCReturnStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY), E_OUTOFMEMORY);

    pVideoProcessorInputView->pResource = pResource;
    pVideoProcessorInputView->svga.ContentDesc = pVideoProcessorEnum->svga.desc;

    VBSVGA3dVPIVDesc *pDesc= &pVideoProcessorInputView->svga.VPIVDesc;
    RT_ZERO(*pDesc);
    pDesc->FourCC               = FourCC;
    pDesc->ViewDimension        = VBSVGA3D_VPIV_DIMENSION_TEXTURE2D;
    pDesc->Texture2D.MipSlice   = MipSlice;
    pDesc->Texture2D.ArraySlice = FirstArraySlice;

    vgpu10DefineVideoProcessorInputView(pDevice, pVideoProcessorInputView->uVideoProcessorInputViewId,
                                        vboxDXGetAllocation(pVideoProcessorInputView->pResource),
                                        pVideoProcessorInputView->svga.ContentDesc, *pDesc);

    pVideoProcessorInputView->fDefined = true;
    RTListAppend(&pVideoProcessorInputView->pResource->listVPIV, &pVideoProcessorInputView->nodeView);
    return S_OK;
}


HRESULT vboxDXCreateVideoProcessorOutputView(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOROUTPUTVIEW pVideoProcessorOutputView,
                                             PVBOXDX_RESOURCE pResource, PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum,
                                             UINT MipSlice, UINT FirstArraySlice, UINT ArraySize)
{
    int rc = RTHandleTableAlloc(pDevice->hHTVideoProcessorOutputView, pVideoProcessorOutputView, &pVideoProcessorOutputView->uVideoProcessorOutputViewId);
    AssertRCReturnStmt(rc, vboxDXDeviceSetError(pDevice, E_OUTOFMEMORY), E_OUTOFMEMORY);

    pVideoProcessorOutputView->pResource = pResource;
    pVideoProcessorOutputView->svga.ContentDesc = pVideoProcessorEnum->svga.desc;

    VBSVGA3dVPOVDesc *pDesc= &pVideoProcessorOutputView->svga.VPOVDesc;
    RT_ZERO(*pDesc);
    if (ArraySize <= 1) /** @todo Or from pResource? */
    {
        pDesc->ViewDimension        = VBSVGA3D_VPOV_DIMENSION_TEXTURE2D;
        pDesc->Texture2D.MipSlice   = MipSlice;
    }
    else
    {
        pDesc->ViewDimension                  = VBSVGA3D_VPOV_DIMENSION_TEXTURE2DARRAY;
        pDesc->Texture2DArray.MipSlice        = MipSlice;
        pDesc->Texture2DArray.FirstArraySlice = FirstArraySlice;
        pDesc->Texture2DArray.ArraySize       = ArraySize;
    }

    vgpu10DefineVideoProcessorOutputView(pDevice, pVideoProcessorOutputView->uVideoProcessorOutputViewId,
                                         vboxDXGetAllocation(pVideoProcessorOutputView->pResource),
                                         pVideoProcessorOutputView->svga.ContentDesc, *pDesc);

    pVideoProcessorOutputView->fDefined = true;
    RTListAppend(&pVideoProcessorOutputView->pResource->listVPOV, &pVideoProcessorOutputView->nodeView);
    return S_OK;
}


HRESULT vboxDXVideoProcessorBlt(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, PVBOXDXVIDEOPROCESSOROUTPUTVIEW pVideoProcessorOutputView,
                                UINT OutputFrame, UINT StreamCount, D3D11_1DDI_VIDEO_PROCESSOR_STREAM const *paStream)
{
    uint32_t cbVideoProcessorStreams = StreamCount * sizeof(VBSVGA3dVideoProcessorStream);
    for (UINT i = 0; i < StreamCount; ++i)
    {
        D3D11_1DDI_VIDEO_PROCESSOR_STREAM const *s = &paStream[i];
        uint32_t cbIds = (s->PastFrames + 1 + s->FutureFrames) * sizeof(VBSVGA3dVideoProcessorInputViewId);
        if (pVideoProcessor->aStreams[i].FrameFormat == D3D11_1DDI_VIDEO_PROCESSOR_STEREO_FORMAT_SEPARATE)
            cbIds *= 2;
        cbVideoProcessorStreams += cbIds;
    }

    void *pvTmpBuffer = RTMemTmpAlloc(cbVideoProcessorStreams);
    if (!pvTmpBuffer)
        return E_OUTOFMEMORY;

    VBSVGA3dVideoProcessorStream *paVideoProcessorStreams = (VBSVGA3dVideoProcessorStream *)pvTmpBuffer;
    for (UINT i = 0; i < StreamCount; ++i)
    {
        D3D11_1DDI_VIDEO_PROCESSOR_STREAM const *s = &paStream[i];
        VBSVGA3dVideoProcessorStream *d = &paVideoProcessorStreams[i];

        d->Enable                      = s->Enable;
        d->StereoFormatSeparate        = pVideoProcessor->aStreams[i].StereoFormat.Format == D3D11_1DDI_VIDEO_PROCESSOR_STEREO_FORMAT_SEPARATE;
        d->pad                         = 0;
        d->OutputIndex                 = s->OutputIndex;
        d->InputFrameOrField           = s->InputFrameOrField;
        d->PastFrames                  = s->PastFrames;
        d->FutureFrames                = s->FutureFrames;

        PVBOXDXVIDEOPROCESSORINPUTVIEW pVideoProcessorInputView;
        VBSVGA3dVideoProcessorInputViewId *pVPIVId = (VBSVGA3dVideoProcessorInputViewId *)&d[1];

        for (UINT j = 0; j < s->PastFrames; ++j)
        {
            pVideoProcessorInputView = (PVBOXDXVIDEOPROCESSORINPUTVIEW)s->pPastSurfaces[j].pDrvPrivate;
            *pVPIVId++ = pVideoProcessorInputView->uVideoProcessorInputViewId;
        }

        pVideoProcessorInputView = (PVBOXDXVIDEOPROCESSORINPUTVIEW)s->hInputSurface.pDrvPrivate;
        *pVPIVId++ = pVideoProcessorInputView->uVideoProcessorInputViewId;

        for (UINT j = 0; j < s->FutureFrames; ++j)
        {
            pVideoProcessorInputView = (PVBOXDXVIDEOPROCESSORINPUTVIEW)s->pFutureSurfaces[j].pDrvPrivate;
            *pVPIVId++ = pVideoProcessorInputView->uVideoProcessorInputViewId;
        }

        if (d->StereoFormatSeparate)
        {
            for (UINT j = 0; j < s->PastFrames; ++j)
            {
                pVideoProcessorInputView = (PVBOXDXVIDEOPROCESSORINPUTVIEW)s->pPastSurfacesRight[j].pDrvPrivate;
                *pVPIVId++ = pVideoProcessorInputView->uVideoProcessorInputViewId;
            }

            pVideoProcessorInputView = (PVBOXDXVIDEOPROCESSORINPUTVIEW)s->hInputSurfaceRight.pDrvPrivate;
            *pVPIVId++ = pVideoProcessorInputView->uVideoProcessorInputViewId;

            for (UINT j = 0; j < s->FutureFrames; ++j)
            {
                pVideoProcessorInputView = (PVBOXDXVIDEOPROCESSORINPUTVIEW)s->pFutureSurfacesRight[j].pDrvPrivate;
                *pVPIVId++ = pVideoProcessorInputView->uVideoProcessorInputViewId;
            }
        }
    }

    vgpu10VideoProcessorBlt(pDevice, pVideoProcessor->uVideoProcessorId, pVideoProcessorOutputView->uVideoProcessorOutputViewId,
                            OutputFrame, StreamCount, cbVideoProcessorStreams, paVideoProcessorStreams);
    RTMemTmpFree(pvTmpBuffer);
    return S_OK;
}


void vboxDXDestroyVideoDecoder(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEODECODER pVideoDecoder)
{
    vgpu10DestroyVideoDecoder(pDevice, pVideoDecoder->uVideoDecoderId);
    RTHandleTableFree(pDevice->hHTVideoDecoder, pVideoDecoder->uVideoDecoderId);
}


void vboxDXDestroyVideoDecoderOutputView(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEODECODEROUTPUTVIEW pVideoDecoderOutputView)
{
    RTListNodeRemove(&pVideoDecoderOutputView->nodeView);

    vgpu10DestroyVideoDecoderOutputView(pDevice, pVideoDecoderOutputView->uVideoDecoderOutputViewId);
    RTHandleTableFree(pDevice->hHTVideoDecoderOutputView, pVideoDecoderOutputView->uVideoDecoderOutputViewId);
}


void vboxDXDestroyVideoProcessor(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor)
{
    vgpu10DestroyVideoProcessor(pDevice, pVideoProcessor->uVideoProcessorId);
    RTHandleTableFree(pDevice->hHTVideoProcessor, pVideoProcessor->uVideoProcessorId);
}


void vboxDXDestroyVideoProcessorInputView(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSORINPUTVIEW pVideoProcessorInputView)
{
    RTListNodeRemove(&pVideoProcessorInputView->nodeView);

    vgpu10DestroyVideoProcessorInputView(pDevice, pVideoProcessorInputView->uVideoProcessorInputViewId);
    RTHandleTableFree(pDevice->hHTVideoProcessorInputView, pVideoProcessorInputView->uVideoProcessorInputViewId);
}


void vboxDXDestroyVideoProcessorOutputView(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOROUTPUTVIEW pVideoProcessorOutputView)
{
    RTListNodeRemove(&pVideoProcessorOutputView->nodeView);

    vgpu10DestroyVideoProcessorOutputView(pDevice, pVideoProcessorOutputView->uVideoProcessorOutputViewId);
    RTHandleTableFree(pDevice->hHTVideoProcessorOutputView, pVideoProcessorOutputView->uVideoProcessorOutputViewId);
}


void vboxDXVideoProcessorSetOutputTargetRect(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, BOOL Enable, RECT const *pOutputRect)
{
    pVideoProcessor->OutputRect.Enable = Enable;
    pVideoProcessor->OutputRect.Rect = *pOutputRect;

    vgpu10VideoProcessorSetOutputTargetRect(pDevice, pVideoProcessor->uVideoProcessorId, Enable, *pOutputRect);
}


void vboxDXVideoProcessorSetOutputBackgroundColor(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, BOOL YCbCr, D3D11_1DDI_VIDEO_COLOR const *pColor)
{
    pVideoProcessor->OutputBackgroundColor.YCbCr = YCbCr;
    pVideoProcessor->OutputBackgroundColor.Color = *pColor;

    vgpu10VideoProcessorSetOutputBackgroundColor(pDevice, pVideoProcessor->uVideoProcessorId, YCbCr, *pColor);
}


void vboxDXVideoProcessorSetOutputColorSpace(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, D3D11_1DDI_VIDEO_PROCESSOR_COLOR_SPACE const *pColorSpace)
{
    pVideoProcessor->Colorspace = *pColorSpace;

    VBSVGA3dVideoProcessorColorSpace svgaColorSpace;
    svgaColorSpace.Usage         = pColorSpace->Usage;
    svgaColorSpace.RGB_Range     = pColorSpace->RGB_Range;
    svgaColorSpace.YCbCr_Matrix  = pColorSpace->YCbCr_Matrix;
    svgaColorSpace.YCbCr_xvYCC   = pColorSpace->YCbCr_xvYCC;
    svgaColorSpace.Nominal_Range = pColorSpace->Nominal_Range;
    svgaColorSpace.Reserved      = 0;

    vgpu10VideoProcessorSetOutputColorSpace(pDevice, pVideoProcessor->uVideoProcessorId, svgaColorSpace);
}


void vboxDXVideoProcessorSetOutputAlphaFillMode(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, D3D11_1DDI_VIDEO_PROCESSOR_ALPHA_FILL_MODE FillMode, UINT StreamIndex)
{
    pVideoProcessor->AlphaFillMode.FillMode = FillMode;
    pVideoProcessor->AlphaFillMode.StreamIndex = StreamIndex;

    VBSVGA3dVideoProcessorAlphaFillMode svgaFillMode = (VBSVGA3dVideoProcessorAlphaFillMode)FillMode;
    vgpu10VideoProcessorSetOutputAlphaFillMode(pDevice, pVideoProcessor->uVideoProcessorId, svgaFillMode, StreamIndex);
}


void vboxDXVideoProcessorSetOutputConstriction(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, BOOL Enabled, SIZE ConstrictonSize)
{
    pVideoProcessor->OutputConstriction.Enabled = Enabled;
    pVideoProcessor->OutputConstriction.ConstrictonSize = ConstrictonSize;

    vgpu10VideoProcessorSetOutputConstriction(pDevice, pVideoProcessor->uVideoProcessorId, Enabled, ConstrictonSize);
}


void vboxDXVideoProcessorSetOutputStereoMode(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, BOOL Enable)
{
    pVideoProcessor->StereoMode.Enable = Enable;

    vgpu10VideoProcessorSetOutputStereoMode(pDevice, pVideoProcessor->uVideoProcessorId, Enable);
}


void vboxDXVideoProcessorSetStreamFrameFormat(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex, D3D11_1DDI_VIDEO_FRAME_FORMAT Format)
{
    pVideoProcessor->aStreams[StreamIndex].FrameFormat = Format;

    VBSVGA3dVideoFrameFormat svgaFormat = (VBSVGA3dVideoFrameFormat)Format;
    vgpu10VideoProcessorSetStreamFrameFormat(pDevice, pVideoProcessor->uVideoProcessorId, StreamIndex, svgaFormat);
}


void vboxDXVideoProcessorSetStreamColorSpace(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex, D3D11_1DDI_VIDEO_PROCESSOR_COLOR_SPACE const *pColorSpace)
{
    pVideoProcessor->aStreams[StreamIndex].ColorSpace = *pColorSpace;

    VBSVGA3dVideoProcessorColorSpace svgaColorSpace;
    svgaColorSpace.Usage         = pColorSpace->Usage;
    svgaColorSpace.RGB_Range     = pColorSpace->RGB_Range;
    svgaColorSpace.YCbCr_Matrix  = pColorSpace->YCbCr_Matrix;
    svgaColorSpace.YCbCr_xvYCC   = pColorSpace->YCbCr_xvYCC;
    svgaColorSpace.Nominal_Range = pColorSpace->Nominal_Range;
    svgaColorSpace.Reserved      = 0;

    vgpu10VideoProcessorSetStreamColorSpace(pDevice, pVideoProcessor->uVideoProcessorId, StreamIndex, svgaColorSpace);
}


void vboxDXVideoProcessorSetStreamOutputRate(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex,
                                             D3D11_1DDI_VIDEO_PROCESSOR_OUTPUT_RATE OutputRate, BOOL RepeatFrame, DXGI_RATIONAL const *pCustomRate)
{
    pVideoProcessor->aStreams[StreamIndex].OutputRate.OutputRate = OutputRate;
    pVideoProcessor->aStreams[StreamIndex].OutputRate.RepeatFrame = RepeatFrame;
    pVideoProcessor->aStreams[StreamIndex].OutputRate.CustomRate = *pCustomRate;

    VBSVGA3dVideoProcessorOutputRate svgaOutputRate = (VBSVGA3dVideoProcessorOutputRate)OutputRate;
    SVGA3dFraction64 svgaCustomRate;
    svgaCustomRate.numerator   = pCustomRate->Numerator;
    svgaCustomRate.denominator = pCustomRate->Denominator;

    vgpu10VideoProcessorSetStreamOutputRate(pDevice, pVideoProcessor->uVideoProcessorId, StreamIndex, svgaOutputRate, RepeatFrame, svgaCustomRate);
}


void vboxDXVideoProcessorSetStreamSourceRect(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex,
                                             BOOL Enable, RECT const *pSourceRect)
{
    pVideoProcessor->aStreams[StreamIndex].SourceRect.Enable = Enable;
    pVideoProcessor->aStreams[StreamIndex].SourceRect.SourceRect = *pSourceRect;

    vgpu10VideoProcessorSetStreamSourceRect(pDevice, pVideoProcessor->uVideoProcessorId, StreamIndex, Enable, pSourceRect);
}


void vboxDXVideoProcessorSetStreamDestRect(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex,
                                           BOOL Enable, RECT const *pDestRect)
{
    pVideoProcessor->aStreams[StreamIndex].DestRect.Enable = Enable;
    pVideoProcessor->aStreams[StreamIndex].DestRect.DestRect = *pDestRect;

    vgpu10VideoProcessorSetStreamDestRect(pDevice, pVideoProcessor->uVideoProcessorId, StreamIndex, Enable, pDestRect);
}


void vboxDXVideoProcessorSetStreamAlpha(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex,
                                        BOOL Enable, FLOAT Alpha)
{
    vgpu10VideoProcessorSetStreamAlpha(pDevice, pVideoProcessor->uVideoProcessorId, StreamIndex, Enable, Alpha);
}


void vboxDXVideoProcessorSetStreamPalette(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex,
                                          UINT Count, UINT const *pEntries)
{
    vgpu10VideoProcessorSetStreamPalette(pDevice, pVideoProcessor->uVideoProcessorId, StreamIndex,
                                         RT_MIN(Count, VBSVGA3D_MAX_VIDEO_PALETTE_ENTRIES), pEntries);
}


void vboxDXVideoProcessorSetStreamPixelAspectRatio(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex,
                                                   BOOL Enable, DXGI_RATIONAL const *pSourceRatio, DXGI_RATIONAL const *pDestRatio)
{
    SVGA3dFraction64 svgaSourceRatio;
    svgaSourceRatio.numerator   = pSourceRatio->Numerator;
    svgaSourceRatio.denominator = pSourceRatio->Denominator;

    SVGA3dFraction64 svgaDestRatio;
    svgaDestRatio.numerator   = pDestRatio->Numerator;
    svgaDestRatio.denominator = pDestRatio->Denominator;

    vgpu10VideoProcessorSetStreamPixelAspectRatio(pDevice, pVideoProcessor->uVideoProcessorId, StreamIndex, Enable, svgaSourceRatio, svgaDestRatio);
}


void vboxDXVideoProcessorSetStreamLumaKey(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex,
                                          BOOL Enable, FLOAT Lower, FLOAT Upper)
{
    pVideoProcessor->aStreams[StreamIndex].LumaKey.Enable = Enable;
    pVideoProcessor->aStreams[StreamIndex].LumaKey.Lower = Lower;
    pVideoProcessor->aStreams[StreamIndex].LumaKey.Upper = Upper;

    vgpu10VideoProcessorSetStreamLumaKey(pDevice, pVideoProcessor->uVideoProcessorId, StreamIndex, Enable, Lower, Upper);
}


void vboxDXVideoProcessorSetStreamStereoFormat(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex, BOOL Enable,
                                               D3D11_1DDI_VIDEO_PROCESSOR_STEREO_FORMAT StereoFormat, BOOL LeftViewFrame0, BOOL BaseViewFrame0,
                                               D3D11_1DDI_VIDEO_PROCESSOR_STEREO_FLIP_MODE FlipMode, int MonoOffset)
{
    pVideoProcessor->aStreams[StreamIndex].StereoFormat.Enable = Enable;
    pVideoProcessor->aStreams[StreamIndex].StereoFormat.Format = StereoFormat;

    VBSVGA3dVideoProcessorStereoFormat svgaStereoFormat = (VBSVGA3dVideoProcessorStereoFormat)StereoFormat;
    VBSVGA3dVideoProcessorStereoFlipMode svgaFlipMode = (VBSVGA3dVideoProcessorStereoFlipMode)FlipMode;

    vgpu10VideoProcessorSetStreamStereoFormat(pDevice, pVideoProcessor->uVideoProcessorId, StreamIndex, Enable,
                                              svgaStereoFormat, LeftViewFrame0, BaseViewFrame0, svgaFlipMode, MonoOffset);
}


void vboxDXVideoProcessorSetStreamAutoProcessingMode(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex, BOOL Enable)
{
    pVideoProcessor->aStreams[StreamIndex].AutoProcessingMode.Enable = Enable;

    vgpu10VideoProcessorSetStreamAutoProcessingMode(pDevice, pVideoProcessor->uVideoProcessorId, StreamIndex, Enable);
}


void vboxDXVideoProcessorSetStreamFilter(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex, BOOL Enable,
                                         D3D11_1DDI_VIDEO_PROCESSOR_FILTER Filter, int Level)
{
    pVideoProcessor->aStreams[StreamIndex].aFilters[Filter].Enable = Enable;
    pVideoProcessor->aStreams[StreamIndex].aFilters[Filter].Level = Level;

    VBSVGA3dVideoProcessorFilter svgaFilter = (VBSVGA3dVideoProcessorFilter)Filter;

    vgpu10VideoProcessorSetStreamFilter(pDevice, pVideoProcessor->uVideoProcessorId, StreamIndex, Enable, svgaFilter, Level);
}


void vboxDXVideoProcessorSetStreamRotation(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex, BOOL Enable,
                                           D3D11_1DDI_VIDEO_PROCESSOR_ROTATION Rotation)
{
    pVideoProcessor->aStreams[StreamIndex].Rotation.Enable = Enable;
    pVideoProcessor->aStreams[StreamIndex].Rotation.Rotation = Rotation;

    VBSVGA3dVideoProcessorRotation svgaRotation = (VBSVGA3dVideoProcessorRotation)Rotation;

    vgpu10VideoProcessorSetStreamRotation(pDevice, pVideoProcessor->uVideoProcessorId, StreamIndex, Enable, svgaRotation);
}
