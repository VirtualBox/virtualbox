/* $Id: VBoxDXDDIVideo.cpp 108773 2025-03-17 15:27:31Z vitali.pelenjow@oracle.com $ */
/** @file
 * VirtualBox D3D11 user mode DDI interface for video.
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

#define WINAPI_FAMILY WINAPI_FAMILY_DESKTOP_APP
#define INITGUID

#include <iprt/alloc.h>
#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <VBox/log.h>

#include <iprt/win/windows.h>
#include <iprt/win/d3dkmthk.h>

#include <d3d10umddi.h>

#include "VBoxDX.h"
#include "VBoxDXCmd.h"

#include <VBoxWddmUmHlp.h>


static VOID APIENTRY ddi11_1GetVideoDecoderProfileCount(
    D3D10DDI_HDEVICE hDevice,
    UINT *pDecodeProfileCount)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();
    vboxDXGetVideoDecoderProfileCount(pDevice, pDecodeProfileCount);
}

static VOID APIENTRY ddi11_1GetVideoDecoderProfile(
    D3D10DDI_HDEVICE hDevice,
    UINT Index,
    GUID *pGuid)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();
    vboxDXGetVideoDecoderProfile(pDevice, Index, pGuid);
}

static VOID APIENTRY ddi11_1CheckVideoDecoderFormat(
    D3D10DDI_HDEVICE hDevice,
    CONST GUID *pDecoderProfile,
    DXGI_FORMAT Format,
    BOOL *pSupported)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();
    vboxDXCheckVideoDecoderFormat(pDevice, pDecoderProfile, Format, pSupported);
}

static VOID APIENTRY ddi11_1GetVideoDecoderConfigCount(
    D3D10DDI_HDEVICE hDevice,
    CONST D3D11_1DDI_VIDEO_DECODER_DESC *pDecodeDesc,
    UINT *pConfigCount)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();
    vboxDXGetVideoDecoderConfigCount(pDevice, pDecodeDesc, pConfigCount);
}

static VOID APIENTRY ddi11_1GetVideoDecoderConfig(
    D3D10DDI_HDEVICE hDevice,
    CONST D3D11_1DDI_VIDEO_DECODER_DESC *pDecodeDesc,
    UINT Index,
    D3D11_1DDI_VIDEO_DECODER_CONFIG *pConfig)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();
    vboxDXGetVideoDecoderConfig(pDevice, pDecodeDesc, Index, pConfig);
}

/* There are no corresponding D3D11 host API so return the hardcoded information about buffers.
 *
 * D3D11_1DDI_VIDEO_DECODER_BUFFER_INFO::Usage has to be D3D11_1DDI_VIDEO_USAGE_OPTIMAL_QUALITY,
 * otherwise Windows refuses to use the decoder.
 */
static D3D11_1DDI_VIDEO_DECODER_BUFFER_INFO const g_aBufferInfo[] =
{
    { D3D11_1DDI_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS,          _64K, D3D11_1DDI_VIDEO_USAGE_OPTIMAL_QUALITY },
    { D3D11_1DDI_VIDEO_DECODER_BUFFER_MACROBLOCK_CONTROL,          _64K, D3D11_1DDI_VIDEO_USAGE_OPTIMAL_QUALITY },
    { D3D11_1DDI_VIDEO_DECODER_BUFFER_RESIDUAL_DIFFERENCE,         _64K, D3D11_1DDI_VIDEO_USAGE_OPTIMAL_QUALITY },
    { D3D11_1DDI_VIDEO_DECODER_BUFFER_DEBLOCKING_CONTROL,          _64K, D3D11_1DDI_VIDEO_USAGE_OPTIMAL_QUALITY },
    { D3D11_1DDI_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX, _64K, D3D11_1DDI_VIDEO_USAGE_OPTIMAL_QUALITY },
    { D3D11_1DDI_VIDEO_DECODER_BUFFER_SLICE_CONTROL,               _64K, D3D11_1DDI_VIDEO_USAGE_OPTIMAL_QUALITY },
    { D3D11_1DDI_VIDEO_DECODER_BUFFER_BITSTREAM,                   _1M, D3D11_1DDI_VIDEO_USAGE_OPTIMAL_QUALITY },
    { D3D11_1DDI_VIDEO_DECODER_BUFFER_MOTION_VECTOR,               _64K, D3D11_1DDI_VIDEO_USAGE_OPTIMAL_QUALITY },
    { D3D11_1DDI_VIDEO_DECODER_BUFFER_FILM_GRAIN,                  _64K, D3D11_1DDI_VIDEO_USAGE_OPTIMAL_QUALITY },
};

static VOID APIENTRY ddi11_1GetVideoDecoderBufferTypeCount(
    D3D10DDI_HDEVICE hDevice,
    CONST D3D11_1DDI_VIDEO_DECODER_DESC *pDecodeDesc,
    UINT *pBufferTypeCount)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, pDecodeDesc);
    *pBufferTypeCount = RT_ELEMENTS(g_aBufferInfo);
}

static VOID APIENTRY ddi11_1GetVideoDecoderBufferInfo(
    D3D10DDI_HDEVICE hDevice,
    CONST D3D11_1DDI_VIDEO_DECODER_DESC *pDecodeDesc,
    UINT Index,
    D3D11_1DDI_VIDEO_DECODER_BUFFER_INFO *pInfo)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice);
    *pInfo = g_aBufferInfo[Index];
    if (pInfo->Type == D3D11_1DDI_VIDEO_DECODER_BUFFER_BITSTREAM)
       pInfo->Size = RT_ALIGN_32(pDecodeDesc->SampleWidth * pDecodeDesc->SampleHeight, 1024 * 1024);
}

static SIZE_T APIENTRY ddi11_1CalcPrivateVideoDecoderSize(
    D3D10DDI_HDEVICE hDevice,
    CONST D3D11_1DDIARG_CREATEVIDEODECODER *pDecoder)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, pDecoder);
    return sizeof(VBOXDXVIDEODECODER);
}

static HRESULT APIENTRY ddi11_1CreateVideoDecoder(
    D3D10DDI_HDEVICE hDevice,
    CONST D3D11_1DDIARG_CREATEVIDEODECODER *pCreateData,
    D3D11_1DDI_HDECODE hDecoder,
    D3D11_1DDI_HRTDECODE hRTDecoder)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEODECODER pVideoDecoder = (PVBOXDXVIDEODECODER)hDecoder.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    RT_ZERO(*pVideoDecoder);
    pVideoDecoder->hRTVideoDecoder = hRTDecoder;
    return vboxDXCreateVideoDecoder(pDevice, pVideoDecoder, pCreateData->Desc, pCreateData->Config);
}

static VOID APIENTRY ddi11_1DestroyVideoDecoder(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HDECODE hDecoder)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEODECODER pVideoDecoder = (PVBOXDXVIDEODECODER)hDecoder.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXDestroyVideoDecoder(pDevice, pVideoDecoder);
}

static HRESULT APIENTRY ddi11_1VideoDecoderExtension(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HDECODE hDecoder,
    CONST D3D11_1DDIARG_VIDEODECODEREXTENSION *pExtension)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, hDecoder, pExtension);
    return E_INVALIDARG; /* Not supported. */
}

static HRESULT APIENTRY ddi11_1VideoDecoderBeginFrame(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HDECODE hDecoder,
    CONST D3D11_1DDIARG_VIDEODECODERBEGINFRAME *pBeginFrame)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEODECODER pVideoDecoder = (PVBOXDXVIDEODECODER)hDecoder.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    pVideoDecoder->Frame.pOutputView = (PVBOXDXVIDEODECODEROUTPUTVIEW)pBeginFrame->hOutputView.pDrvPrivate;
    return vboxDXVideoDecoderBeginFrame(pDevice, pVideoDecoder,
                                        pVideoDecoder->Frame.pOutputView,
                                        pBeginFrame->pContentKey, pBeginFrame->ContentKeySize);
}

static VOID APIENTRY ddi11_1VideoDecoderEndFrame(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HDECODE hDecoder)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEODECODER pVideoDecoder = (PVBOXDXVIDEODECODER)hDecoder.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    pVideoDecoder->Frame.pOutputView = NULL;
    vboxDXVideoDecoderEndFrame(pDevice, pVideoDecoder);
}

static HRESULT APIENTRY ddi11_1VideoDecoderSubmitBuffers(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HDECODE hDecoder,
    UINT BufferCount,
    CONST D3D11_1DDI_VIDEO_DECODER_BUFFER_DESC *pBufferDesc)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEODECODER pVideoDecoder = (PVBOXDXVIDEODECODER)hDecoder.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

#ifdef LOG_ENABLED
    for (UINT i = 0; i < BufferCount; ++i)
    {
        D3D11_1DDI_VIDEO_DECODER_BUFFER_DESC const *pDesc = &pBufferDesc[i];
        LogFlowFunc(("at %d, size %d\n", pDesc->DataOffset, pDesc->DataSize));
        RT_NOREF(pDesc);
    }
#endif

    return vboxDXVideoDecoderSubmitBuffers(pDevice, pVideoDecoder, BufferCount, pBufferDesc);
}

static SIZE_T APIENTRY ddi11_1CalcPrivateVideoProcessorEnumSize(
    D3D10DDI_HDEVICE hDevice,
    CONST D3D11_1DDIARG_CREATEVIDEOPROCESSORENUM *pProcessorEnum)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, pProcessorEnum);
    return sizeof(VBOXDXVIDEOPROCESSORENUM);
}

static HRESULT APIENTRY ddi11_1CreateVideoProcessorEnum(
    D3D10DDI_HDEVICE hDevice,
    CONST D3D11_1DDIARG_CREATEVIDEOPROCESSORENUM *pCreateData,
    D3D11_1DDI_HVIDEOPROCESSORENUM hVideoProcessorEnum,
    D3D11_1DDI_HRTVIDEOPROCESSORENUM hRTVideoProcessorEnum)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum = (PVBOXDXVIDEOPROCESSORENUM)hVideoProcessorEnum.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    RT_ZERO(*pVideoProcessorEnum);
    pVideoProcessorEnum->hRTVideoProcessorEnum = hRTVideoProcessorEnum;
    return vboxDXCreateVideoProcessorEnum(pDevice, pVideoProcessorEnum, &pCreateData->Desc);
}

static VOID APIENTRY ddi11_1DestroyVideoProcessorEnum(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSORENUM hProcessorEnum)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum = (PVBOXDXVIDEOPROCESSORENUM)hProcessorEnum.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    RT_NOREF(pDevice);
    RT_ZERO(*pVideoProcessorEnum);
}

static VOID APIENTRY ddi11_1CheckVideoProcessorFormat(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSORENUM hVideoProcessorEnum,
    DXGI_FORMAT Format,
    UINT *pSupported)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum = (PVBOXDXVIDEOPROCESSORENUM)hVideoProcessorEnum.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXCheckVideoProcessorFormat(pDevice, pVideoProcessorEnum, Format, pSupported);
}

static VOID APIENTRY ddi11_1GetVideoProcessorCaps(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSORENUM hProcessorEnum,
    D3D11_1DDI_VIDEO_PROCESSOR_CAPS *pCaps)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum = (PVBOXDXVIDEOPROCESSORENUM)hProcessorEnum.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXGetVideoProcessorCaps(pDevice, pVideoProcessorEnum, pCaps);
}

static VOID APIENTRY ddi11_1GetVideoProcessorRateConversionCaps(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSORENUM hProcessorEnum,
    UINT RateConversionIndex,
    D3D11_1DDI_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS *pCaps)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum = (PVBOXDXVIDEOPROCESSORENUM)hProcessorEnum.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    RT_NOREF(RateConversionIndex); /* One capability. */
    vboxDXGetVideoProcessorRateConversionCaps(pDevice, pVideoProcessorEnum, pCaps);
}

static VOID APIENTRY ddi11_1GetVideoProcessorCustomRate(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSORENUM hProcessorEnum,
    UINT RateConversionIndex,
    UINT CustomRateIndex,
    D3D11_1DDI_VIDEO_PROCESSOR_CUSTOM_RATE *pRate)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum = (PVBOXDXVIDEOPROCESSORENUM)hProcessorEnum.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    RT_NOREF(RateConversionIndex); /* One capability. */
    vboxDXGetVideoProcessorCustomRate(pDevice, pVideoProcessorEnum, CustomRateIndex, pRate);
}

static VOID APIENTRY ddi11_1GetVideoProcessorFilterRange(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSORENUM hProcessorEnum,
    D3D11_1DDI_VIDEO_PROCESSOR_FILTER Filter,
    D3D11_1DDI_VIDEO_PROCESSOR_FILTER_RANGE *pFilterRange)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum = (PVBOXDXVIDEOPROCESSORENUM)hProcessorEnum.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXGetVideoProcessorFilterRange(pDevice, pVideoProcessorEnum, Filter, pFilterRange);
}

static SIZE_T APIENTRY ddi11_1CalcPrivateVideoProcessorSize(
    D3D10DDI_HDEVICE hDevice,
    CONST D3D11_1DDIARG_CREATEVIDEOPROCESSOR *pVideoProcessor)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, pVideoProcessor);
    return sizeof(VBOXDXVIDEOPROCESSOR);
}

static HRESULT APIENTRY ddi11_1CreateVideoProcessor(
    D3D10DDI_HDEVICE hDevice,
    CONST D3D11_1DDIARG_CREATEVIDEOPROCESSOR *pCreateData,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    D3D11_1DDI_HRTVIDEOPROCESSOR hRTVideoProcessor)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum = (PVBOXDXVIDEOPROCESSORENUM)pCreateData->hVideoProcessorEnum.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOR pVideoProcessor = (PVBOXDXVIDEOPROCESSOR)hVideoProcessor.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    RT_ZERO(*pVideoProcessor);
    pVideoProcessor->hRTVideoProcessor = hRTVideoProcessor;
    return vboxDXCreateVideoProcessor(pDevice, pVideoProcessor, pVideoProcessorEnum, pCreateData->RateConversionCapsIndex);
}

static VOID APIENTRY ddi11_1DestroyVideoProcessor(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOR pVideoProcessor = (PVBOXDXVIDEOPROCESSOR)hVideoProcessor.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXDestroyVideoProcessor(pDevice, pVideoProcessor);
}

static VOID APIENTRY ddi11_1VideoProcessorSetOutputTargetRect(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    BOOL Enable,
    CONST RECT *pOutputRect)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOR pVideoProcessor = (PVBOXDXVIDEOPROCESSOR)hVideoProcessor.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXVideoProcessorSetOutputTargetRect(pDevice, pVideoProcessor, Enable, pOutputRect);
}

static VOID APIENTRY ddi11_1VideoProcessorSetOutputBackgroundColor(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    BOOL YCbCr,
    CONST D3D11_1DDI_VIDEO_COLOR *pColor)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOR pVideoProcessor = (PVBOXDXVIDEOPROCESSOR)hVideoProcessor.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXVideoProcessorSetOutputBackgroundColor(pDevice, pVideoProcessor, YCbCr, pColor);
}

static VOID APIENTRY ddi11_1VideoProcessorSetOutputColorSpace(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    CONST D3D11_1DDI_VIDEO_PROCESSOR_COLOR_SPACE *pColorspace)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOR pVideoProcessor = (PVBOXDXVIDEOPROCESSOR)hVideoProcessor.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXVideoProcessorSetOutputColorSpace(pDevice, pVideoProcessor, pColorspace);
}

static VOID APIENTRY ddi11_1VideoProcessorSetOutputAlphaFillMode(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    D3D11_1DDI_VIDEO_PROCESSOR_ALPHA_FILL_MODE FillMode,
    UINT StreamIndex)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOR pVideoProcessor = (PVBOXDXVIDEOPROCESSOR)hVideoProcessor.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXVideoProcessorSetOutputAlphaFillMode(pDevice, pVideoProcessor, FillMode, StreamIndex);
}

static VOID APIENTRY ddi11_1VideoProcessorSetOutputConstriction(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    BOOL Enabled,
    SIZE ConstrictonSize)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOR pVideoProcessor = (PVBOXDXVIDEOPROCESSOR)hVideoProcessor.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXVideoProcessorSetOutputConstriction(pDevice, pVideoProcessor, Enabled, ConstrictonSize);
}

static VOID APIENTRY ddi11_1VideoProcessorSetOutputStereoMode(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    BOOL Enable)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOR pVideoProcessor = (PVBOXDXVIDEOPROCESSOR)hVideoProcessor.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXVideoProcessorSetOutputStereoMode(pDevice, pVideoProcessor, Enable);
}

static HRESULT APIENTRY ddi11_1VideoProcessorSetOutputExtension(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    CONST GUID *pGuid,
    UINT DataSize,
    void *pData)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, hVideoProcessor, pGuid, DataSize, pData);
    return S_OK;
}

static HRESULT APIENTRY ddi11_1VideoProcessorGetOutputExtension(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    CONST GUID *pGuid,
    UINT DataSize,
    void *pData)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, hVideoProcessor, pGuid, DataSize, pData);
    return S_OK;
}

static VOID APIENTRY ddi11_1VideoProcessorSetStreamFrameFormat(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    UINT StreamIndex,
    D3D11_1DDI_VIDEO_FRAME_FORMAT Format)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOR pVideoProcessor = (PVBOXDXVIDEOPROCESSOR)hVideoProcessor.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXVideoProcessorSetStreamFrameFormat(pDevice, pVideoProcessor, StreamIndex, Format);
}

static VOID APIENTRY ddi11_1VideoProcessorSetStreamColorSpace(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    UINT StreamIndex,
    CONST D3D11_1DDI_VIDEO_PROCESSOR_COLOR_SPACE *pColorSpace)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOR pVideoProcessor = (PVBOXDXVIDEOPROCESSOR)hVideoProcessor.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXVideoProcessorSetStreamColorSpace(pDevice, pVideoProcessor, StreamIndex, pColorSpace);
}

static VOID APIENTRY ddi11_1VideoProcessorSetStreamOutputRate(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    UINT StreamIndex,
    D3D11_1DDI_VIDEO_PROCESSOR_OUTPUT_RATE OutputRate,
    BOOL RepeatFrame,
    CONST DXGI_RATIONAL *pCustomRate)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOR pVideoProcessor = (PVBOXDXVIDEOPROCESSOR)hVideoProcessor.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXVideoProcessorSetStreamOutputRate(pDevice, pVideoProcessor, StreamIndex, OutputRate, RepeatFrame, pCustomRate);
}

static VOID APIENTRY ddi11_1VideoProcessorSetStreamSourceRect(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    UINT StreamIndex,
    BOOL Enable,
    CONST RECT *pSourceRect)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOR pVideoProcessor = (PVBOXDXVIDEOPROCESSOR)hVideoProcessor.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXVideoProcessorSetStreamSourceRect(pDevice, pVideoProcessor, StreamIndex, Enable, pSourceRect);
}

static VOID APIENTRY ddi11_1VideoProcessorSetStreamDestRect(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    UINT StreamIndex,
    BOOL Enable,
    CONST RECT *pDestRect)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOR pVideoProcessor = (PVBOXDXVIDEOPROCESSOR)hVideoProcessor.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXVideoProcessorSetStreamDestRect(pDevice, pVideoProcessor, StreamIndex, Enable, pDestRect);
}

static VOID APIENTRY ddi11_1VideoProcessorSetStreamAlpha(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    UINT StreamIndex,
    BOOL Enable,
    FLOAT Alpha)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOR pVideoProcessor = (PVBOXDXVIDEOPROCESSOR)hVideoProcessor.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXVideoProcessorSetStreamAlpha(pDevice, pVideoProcessor, StreamIndex, Enable, Alpha);
}

static VOID APIENTRY ddi11_1VideoProcessorSetStreamPalette(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    UINT StreamIndex,
    UINT Count,
    CONST UINT *pEntries)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOR pVideoProcessor = (PVBOXDXVIDEOPROCESSOR)hVideoProcessor.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXVideoProcessorSetStreamPalette(pDevice, pVideoProcessor, StreamIndex, Count, pEntries);
}

static VOID APIENTRY ddi11_1VideoProcessorSetStreamPixelAspectRatio(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    UINT StreamIndex,
    BOOL Enable,
    CONST DXGI_RATIONAL *pSourceRatio,
    CONST DXGI_RATIONAL *pDestRatio)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOR pVideoProcessor = (PVBOXDXVIDEOPROCESSOR)hVideoProcessor.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXVideoProcessorSetStreamPixelAspectRatio(pDevice, pVideoProcessor, StreamIndex, Enable, pSourceRatio, pDestRatio);
}

static VOID APIENTRY ddi11_1VideoProcessorSetStreamLumaKey(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    UINT StreamIndex,
    BOOL Enable,
    FLOAT Lower,
    FLOAT Upper)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOR pVideoProcessor = (PVBOXDXVIDEOPROCESSOR)hVideoProcessor.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXVideoProcessorSetStreamLumaKey(pDevice, pVideoProcessor, StreamIndex, Enable, Lower, Upper);
}

static VOID APIENTRY ddi11_1VideoProcessorSetStreamStereoFormat(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    UINT StreamIndex,
    BOOL Enable,
    D3D11_1DDI_VIDEO_PROCESSOR_STEREO_FORMAT StereoFormat,
    BOOL LeftViewFrame0,
    BOOL BaseViewFrame0,
    D3D11_1DDI_VIDEO_PROCESSOR_STEREO_FLIP_MODE FlipMode,
    int MonoOffset)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOR pVideoProcessor = (PVBOXDXVIDEOPROCESSOR)hVideoProcessor.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXVideoProcessorSetStreamStereoFormat(pDevice, pVideoProcessor, StreamIndex, Enable,
                                              StereoFormat, LeftViewFrame0, BaseViewFrame0, FlipMode, MonoOffset);
}

static VOID APIENTRY ddi11_1VideoProcessorSetStreamAutoProcessingMode(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    UINT StreamIndex,
    BOOL Enable)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOR pVideoProcessor = (PVBOXDXVIDEOPROCESSOR)hVideoProcessor.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXVideoProcessorSetStreamAutoProcessingMode(pDevice, pVideoProcessor, StreamIndex, Enable);
}

static VOID APIENTRY ddi11_1VideoProcessorSetStreamFilter(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    UINT StreamIndex,
    D3D11_1DDI_VIDEO_PROCESSOR_FILTER Filter,
    BOOL Enable,
    int Level)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOR pVideoProcessor = (PVBOXDXVIDEOPROCESSOR)hVideoProcessor.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXVideoProcessorSetStreamFilter(pDevice, pVideoProcessor, StreamIndex, Enable, Filter, Level);
}

static VOID APIENTRY ddi11_1VideoProcessorSetStreamRotation(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    UINT StreamIndex,
    BOOL Enable,
    D3D11_1DDI_VIDEO_PROCESSOR_ROTATION Rotation)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOR pVideoProcessor = (PVBOXDXVIDEOPROCESSOR)hVideoProcessor.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXVideoProcessorSetStreamRotation(pDevice, pVideoProcessor, StreamIndex, Enable, Rotation);
}

static HRESULT APIENTRY ddi11_1VideoProcessorSetStreamExtension(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    UINT StreamIndex,
    CONST GUID *pGuid,
    UINT DataSize,
    void *pData)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, hVideoProcessor, StreamIndex, pGuid, DataSize, pData);
    return E_INVALIDARG; /* Not supported. */
}

static HRESULT APIENTRY ddi11_1VideoProcessorGetStreamExtension(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    UINT StreamIndex,
    CONST GUID *pGuid,
    UINT DataSize,
    void *pData)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, hVideoProcessor, StreamIndex, pGuid, DataSize, pData);
    return E_INVALIDARG; /* Not supported. */
}

static HRESULT APIENTRY ddi11_1VideoProcessorBlt(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOR hVideoProcessor,
    D3D11_1DDI_HVIDEOPROCESSOROUTPUTVIEW hOutputView,
    UINT OutputFrame,
    UINT StreamCount,
    CONST D3D11_1DDI_VIDEO_PROCESSOR_STREAM *pStream)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOR pVideoProcessor = (PVBOXDXVIDEOPROCESSOR)hVideoProcessor.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOROUTPUTVIEW pVideoProcessorOutputView = (PVBOXDXVIDEOPROCESSOROUTPUTVIEW)hOutputView.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    return vboxDXVideoProcessorBlt(pDevice, pVideoProcessor, pVideoProcessorOutputView, OutputFrame, StreamCount, pStream);
}

static SIZE_T APIENTRY ddi11_1CalcPrivateVideoDecoderOutputViewSize(
    D3D10DDI_HDEVICE hDevice,
    CONST D3D11_1DDIARG_CREATEVIDEODECODEROUTPUTVIEW *pView)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, pView);
    return sizeof(VBOXDXVIDEODECODEROUTPUTVIEW);
}

static HRESULT APIENTRY ddi11_1CreateVideoDecoderOutputView(
    D3D10DDI_HDEVICE hDevice,
    CONST D3D11_1DDIARG_CREATEVIDEODECODEROUTPUTVIEW *pCreateData,
    D3D11_1DDI_HVIDEODECODEROUTPUTVIEW hView,
    D3D11_1DDI_HRTVIDEODECODEROUTPUTVIEW hRTView)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)pCreateData->hDrvResource.pDrvPrivate;
    PVBOXDXVIDEODECODEROUTPUTVIEW pVideoDecoderOutputView = (PVBOXDXVIDEODECODEROUTPUTVIEW)hView.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    RT_ZERO(*pVideoDecoderOutputView);
    pVideoDecoderOutputView->hRTView = hRTView;
    return vboxDXCreateVideoDecoderOutputView(pDevice, pVideoDecoderOutputView, pResource,
                                              pCreateData->DecodeProfile, pCreateData->MipSlice, pCreateData->FirstArraySlice, pCreateData->ArraySize);
}

static VOID APIENTRY ddi11_1DestroyVideoDecoderOutputView(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEODECODEROUTPUTVIEW hView)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEODECODEROUTPUTVIEW pVideoDecoderOutputView = (PVBOXDXVIDEODECODEROUTPUTVIEW)hView.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXDestroyVideoDecoderOutputView(pDevice, pVideoDecoderOutputView);
}

static SIZE_T APIENTRY ddi11_1CalcPrivateVideoProcessorInputViewSize(
    D3D10DDI_HDEVICE hDevice,
    CONST D3D11_1DDIARG_CREATEVIDEOPROCESSORINPUTVIEW *pView)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, pView);
    return sizeof(VBOXDXVIDEOPROCESSORINPUTVIEW);
}

static HRESULT APIENTRY ddi11_1CreateVideoProcessorInputView(
    D3D10DDI_HDEVICE hDevice,
    CONST D3D11_1DDIARG_CREATEVIDEOPROCESSORINPUTVIEW *pCreateData,
    D3D11_1DDI_HVIDEOPROCESSORINPUTVIEW hView,
    D3D11_1DDI_HRTVIDEOPROCESSORINPUTVIEW hRTView)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)pCreateData->hDrvResource.pDrvPrivate;
    PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum = (PVBOXDXVIDEOPROCESSORENUM)pCreateData->hDrvVideoProcessorEnum.pDrvPrivate;
    PVBOXDXVIDEOPROCESSORINPUTVIEW pVideoProcessorInputView = (PVBOXDXVIDEOPROCESSORINPUTVIEW)hView.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    pVideoProcessorInputView->hRTView = hRTView;
    return vboxDXCreateVideoProcessorInputView(pDevice, pVideoProcessorInputView, pResource, pVideoProcessorEnum,
                                               pCreateData->FourCC, pCreateData->MipSlice, pCreateData->FirstArraySlice, pCreateData->ArraySize);
}

static VOID APIENTRY ddi11_1DestroyVideoProcessorInputView(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSORINPUTVIEW hView)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSORINPUTVIEW pVideoProcessorInputView = (PVBOXDXVIDEOPROCESSORINPUTVIEW)hView.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXDestroyVideoProcessorInputView(pDevice, pVideoProcessorInputView);
}

static SIZE_T APIENTRY ddi11_1CalcPrivateVideoProcessorOutputViewSize(
    D3D10DDI_HDEVICE hDevice,
    CONST D3D11_1DDIARG_CREATEVIDEOPROCESSOROUTPUTVIEW *pView)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, pView);
    return sizeof(VBOXDXVIDEOPROCESSOROUTPUTVIEW);
}

static HRESULT APIENTRY ddi11_1CreateVideoProcessorOutputView(
    D3D10DDI_HDEVICE hDevice,
    CONST D3D11_1DDIARG_CREATEVIDEOPROCESSOROUTPUTVIEW *pCreateData,
    D3D11_1DDI_HVIDEOPROCESSOROUTPUTVIEW hView,
    D3D11_1DDI_HRTVIDEOPROCESSOROUTPUTVIEW hRTView)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)pCreateData->hDrvResource.pDrvPrivate;
    PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum = (PVBOXDXVIDEOPROCESSORENUM)pCreateData->hDrvVideoProcessorEnum.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOROUTPUTVIEW pVideoProcessorOutputView = (PVBOXDXVIDEOPROCESSOROUTPUTVIEW)hView.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    pVideoProcessorOutputView->hRTView = hRTView;
    return vboxDXCreateVideoProcessorOutputView(pDevice, pVideoProcessorOutputView, pResource, pVideoProcessorEnum,
                                                pCreateData->MipSlice, pCreateData->FirstArraySlice, pCreateData->ArraySize);
}

static VOID APIENTRY ddi11_1DestroyVideoProcessorOutputView(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSOROUTPUTVIEW hView)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOPROCESSOROUTPUTVIEW pVideoProcessorOutputView = (PVBOXDXVIDEOPROCESSOROUTPUTVIEW)hView.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    vboxDXDestroyVideoProcessorOutputView(pDevice, pVideoProcessorOutputView);
}

static VOID APIENTRY ddi11_1VideoProcessorInputViewReadAfterWriteHazard(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HVIDEOPROCESSORINPUTVIEW hView,
    D3D10DDI_HRESOURCE hResource)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDX_RESOURCE pResource = (PVBOXDX_RESOURCE)hResource.pDrvPrivate;
    PVBOXDXVIDEOPROCESSORINPUTVIEW pVideoProcessorInputView = (PVBOXDXVIDEOPROCESSORINPUTVIEW)hView.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();

    RT_NOREF(pDevice, pVideoProcessorInputView, pResource);
}

static HRESULT APIENTRY ddi11_1GetContentProtectionCaps(
    D3D10DDI_HDEVICE hDevice,
    CONST GUID *pCryptoType,
    CONST GUID *pDecodeProfile,
    D3D11_1DDI_VIDEO_CONTENT_PROTECTION_CAPS *pCaps)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, pCryptoType, pDecodeProfile, pCaps);
    return D3DERR_UNSUPPORTEDCRYPTO;
}

static HRESULT APIENTRY ddi11_1GetCryptoKeyExchangeType(
    D3D10DDI_HDEVICE hDevice,
    CONST GUID *pCryptoType,
    CONST GUID *pDecodeProfile,
    UINT Index,
    GUID *pKeyExchangeType)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, pCryptoType, pDecodeProfile, Index, pKeyExchangeType);
    return D3DERR_UNSUPPORTEDCRYPTO;
}

static SIZE_T APIENTRY ddi11_1CalcPrivateCryptoSessionSize(
    D3D10DDI_HDEVICE hDevice,
    CONST D3D11_1DDIARG_CREATECRYPTOSESSION *pCreateData)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, pCreateData);
    return sizeof(VBOXDXVIDEOCRYPTOSESSION);
}

static HRESULT APIENTRY ddi11_1CreateCryptoSession(
    D3D10DDI_HDEVICE hDevice,
    CONST D3D11_1DDIARG_CREATECRYPTOSESSION *pCreateData,
    D3D11_1DDI_HCRYPTOSESSION hCryptoSession,
    D3D11_1DDI_HRTCRYPTOSESSION hRTCryptoSession)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOCRYPTOSESSION pCryptoSession = (PVBOXDXVIDEOCRYPTOSESSION)hCryptoSession.pDrvPrivate;
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice);
    RT_ZERO(*pCryptoSession);
    pCryptoSession->hRTCryptoSession = hRTCryptoSession;
    RT_NOREF(pCreateData);
    return D3DDDIERR_UNSUPPORTEDCRYPTO;
}

static VOID APIENTRY ddi11_1DestroyCryptoSession(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HCRYPTOSESSION hCryptoSession)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, hCryptoSession);
    return;
}

static VOID APIENTRY ddi11_1GetCertificateSize(
    D3D10DDI_HDEVICE hDevice,
    CONST D3D11_1DDI_CERTIFICATE_INFO *pCertificateInfo,
    UINT *pCertificateSize)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, pCertificateInfo);
    *pCertificateSize = 0;
    return;
}

static VOID APIENTRY ddi11_1GetCertificate(
    D3D10DDI_HDEVICE hDevice,
    CONST D3D11_1DDI_CERTIFICATE_INFO *pCertificateInfo,
    UINT CertificateSize,
    BYTE *pCertificate)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, pCertificateInfo, CertificateSize, pCertificate);
    return;
}

static HRESULT APIENTRY ddi11_1NegotiateCryptoSessionKeyExchange(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HCRYPTOSESSION hCryptoSession,
    UINT DataSize,
    BYTE *pData)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, hCryptoSession, DataSize ,pData);
    return S_OK;
}

static VOID APIENTRY ddi11_1EncryptionBlt(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HCRYPTOSESSION hCryptoSession,
    D3D10DDI_HRESOURCE hSrcResource,
    D3D10DDI_HRESOURCE hDstResource,
    UINT IVSize,
    CONST VOID *pIV)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, hCryptoSession, hSrcResource, hDstResource, IVSize, pIV);
    return;
}

static VOID APIENTRY ddi11_1DecryptionBlt(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HCRYPTOSESSION hCryptoSession,
    D3D10DDI_HRESOURCE hSrcResource,
    D3D10DDI_HRESOURCE hDstResource,
    CONST D3D11_1DDI_ENCRYPTED_BLOCK_INFO *pEncryptedBlockInfo,
    UINT ContentKeySize,
    CONST VOID *pContentKey,
    UINT IVSize,
    CONST VOID *pIV)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, hCryptoSession, hSrcResource, hDstResource, pEncryptedBlockInfo, ContentKeySize, pContentKey, IVSize, pIV);
    return;
}

static VOID APIENTRY ddi11_1StartSessionKeyRefresh(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HCRYPTOSESSION hCryptoSession,
    UINT RandomNumberSize,
    VOID *pRandomNumber)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, hCryptoSession, RandomNumberSize, pRandomNumber);
    return;
}

static VOID APIENTRY ddi11_1FinishSessionKeyRefresh(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HCRYPTOSESSION hCryptoSession)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, hCryptoSession);
    return;
}

static VOID APIENTRY ddi11_1GetEncryptionBltKey(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HCRYPTOSESSION hCryptoSession,
    UINT KeySize,
    VOID *pReadbackKey)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, hCryptoSession, KeySize, pReadbackKey);
    return;
}

static SIZE_T APIENTRY ddi11_1CalcPrivateAuthenticatedChannelSize(
    D3D10DDI_HDEVICE hDevice,
    CONST D3D11_1DDIARG_CREATEAUTHENTICATEDCHANNEL *pCreateData)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, pCreateData);
    return sizeof(VBOXDXVIDEOAUTHCHANNEL);
}

static HRESULT APIENTRY ddi11_1CreateAuthenticatedChannel(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDIARG_CREATEAUTHENTICATEDCHANNEL *pCreateData,
    D3D11_1DDI_HAUTHCHANNEL hAuthChannel,
    D3D11_1DDI_HRTAUTHCHANNEL hRTAuthChannel)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    PVBOXDXVIDEOAUTHCHANNEL pAuthChannel = (PVBOXDXVIDEOAUTHCHANNEL)hAuthChannel.pDrvPrivate;
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice);
    RT_ZERO(*pAuthChannel);
    pAuthChannel->hRTAuthChannel = hRTAuthChannel;
    RT_NOREF(pCreateData);
    return S_OK;
}

static VOID APIENTRY ddi11_1DestroyAuthenticatedChannel(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HAUTHCHANNEL hAuthChannel)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, hAuthChannel);
    return;
}

static HRESULT APIENTRY ddi11_1NegotiateAuthenticatedChannelKeyExchange(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HAUTHCHANNEL hAuthChannel,
    UINT DataSize,
    VOID *pData)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, hAuthChannel, DataSize, pData);
    return S_OK;
}

static HRESULT APIENTRY ddi11_1QueryAuthenticatedChannel(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HAUTHCHANNEL hAuthChannel,
    UINT InputDataSize,
    CONST VOID *pInputData,
    UINT OutputDataSize,
    VOID *pOutputData)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, hAuthChannel, InputDataSize, pInputData, OutputDataSize, pOutputData);
    return E_FAIL;
}

static HRESULT APIENTRY ddi11_1ConfigureAuthenticatedChannel(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HAUTHCHANNEL hAuthChannel,
    UINT InputDataSize,
    CONST VOID *pInputData,
    D3D11_1DDI_AUTHENTICATED_CONFIGURE_OUTPUT *pOutputData)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, hAuthChannel, InputDataSize, pInputData, pOutputData);
    return E_FAIL;
}

static HRESULT APIENTRY ddi11_1VideoDecoderGetHandle(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HDECODE hDecoder,
    HANDLE *pContentProtectionHandle)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, hDecoder, pContentProtectionHandle);
    return S_OK;
}

static HRESULT APIENTRY ddi11_1CryptoSessionGetHandle(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_HCRYPTOSESSION hCryptoSession,
    HANDLE *pHandle)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, hCryptoSession, pHandle);
    return S_OK;
}

static VOID APIENTRY ddi11_1GetCaptureHandle(
    D3D10DDI_HDEVICE hDevice,
    D3D11_1DDI_GETCAPTUREHANDLEDATA *pHandleData)
{
    PVBOXDX_DEVICE pDevice = (PVBOXDX_DEVICE)hDevice.pDrvPrivate;
    DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice, pHandleData);
    return;
}

HRESULT ddi11_1RetrieveVideoFunctions(PVBOXDX_DEVICE pDevice, D3D11_1DDI_VIDEO_INPUT *pVideoInput)
{
    //DEBUG_BREAKPOINT_TEST();
    RT_NOREF(pDevice);

    D3D11_1DDI_VIDEODEVICEFUNCS *p = pVideoInput->p11VideoDeviceFuncs;

    p->pfnGetVideoDecoderProfileCount                 = ddi11_1GetVideoDecoderProfileCount;
    p->pfnGetVideoDecoderProfile                      = ddi11_1GetVideoDecoderProfile;
    p->pfnCheckVideoDecoderFormat                     = ddi11_1CheckVideoDecoderFormat;
    p->pfnGetVideoDecoderConfigCount                  = ddi11_1GetVideoDecoderConfigCount;
    p->pfnGetVideoDecoderConfig                       = ddi11_1GetVideoDecoderConfig;
    p->pfnGetVideoDecoderBufferTypeCount              = ddi11_1GetVideoDecoderBufferTypeCount;
    p->pfnGetVideoDecoderBufferInfo                   = ddi11_1GetVideoDecoderBufferInfo;
    p->pfnCalcPrivateVideoDecoderSize                 = ddi11_1CalcPrivateVideoDecoderSize;
    p->pfnCreateVideoDecoder                          = ddi11_1CreateVideoDecoder;
    p->pfnDestroyVideoDecoder                         = ddi11_1DestroyVideoDecoder;
    p->pfnVideoDecoderExtension                       = ddi11_1VideoDecoderExtension;
    p->pfnVideoDecoderBeginFrame                      = ddi11_1VideoDecoderBeginFrame;
    p->pfnVideoDecoderEndFrame                        = ddi11_1VideoDecoderEndFrame;
    p->pfnVideoDecoderSubmitBuffers                   = ddi11_1VideoDecoderSubmitBuffers;
    p->pfnCalcPrivateVideoProcessorEnumSize           = ddi11_1CalcPrivateVideoProcessorEnumSize;
    p->pfnCreateVideoProcessorEnum                    = ddi11_1CreateVideoProcessorEnum;
    p->pfnDestroyVideoProcessorEnum                   = ddi11_1DestroyVideoProcessorEnum;
    p->pfnCheckVideoProcessorFormat                   = ddi11_1CheckVideoProcessorFormat;
    p->pfnGetVideoProcessorCaps                       = ddi11_1GetVideoProcessorCaps;
    p->pfnGetVideoProcessorRateConversionCaps         = ddi11_1GetVideoProcessorRateConversionCaps;
    p->pfnGetVideoProcessorCustomRate                 = ddi11_1GetVideoProcessorCustomRate;
    p->pfnGetVideoProcessorFilterRange                = ddi11_1GetVideoProcessorFilterRange;
    p->pfnCalcPrivateVideoProcessorSize               = ddi11_1CalcPrivateVideoProcessorSize;
    p->pfnCreateVideoProcessor                        = ddi11_1CreateVideoProcessor;
    p->pfnDestroyVideoProcessor                       = ddi11_1DestroyVideoProcessor;
    p->pfnVideoProcessorSetOutputTargetRect           = ddi11_1VideoProcessorSetOutputTargetRect;
    p->pfnVideoProcessorSetOutputBackgroundColor      = ddi11_1VideoProcessorSetOutputBackgroundColor;
    p->pfnVideoProcessorSetOutputColorSpace           = ddi11_1VideoProcessorSetOutputColorSpace;
    p->pfnVideoProcessorSetOutputAlphaFillMode        = ddi11_1VideoProcessorSetOutputAlphaFillMode;
    p->pfnVideoProcessorSetOutputConstriction         = ddi11_1VideoProcessorSetOutputConstriction;
    p->pfnVideoProcessorSetOutputStereoMode           = ddi11_1VideoProcessorSetOutputStereoMode;
    p->pfnVideoProcessorSetOutputExtension            = ddi11_1VideoProcessorSetOutputExtension;
    p->pfnVideoProcessorGetOutputExtension            = ddi11_1VideoProcessorGetOutputExtension;
    p->pfnVideoProcessorSetStreamFrameFormat          = ddi11_1VideoProcessorSetStreamFrameFormat;
    p->pfnVideoProcessorSetStreamColorSpace           = ddi11_1VideoProcessorSetStreamColorSpace;
    p->pfnVideoProcessorSetStreamOutputRate           = ddi11_1VideoProcessorSetStreamOutputRate;
    p->pfnVideoProcessorSetStreamSourceRect           = ddi11_1VideoProcessorSetStreamSourceRect;
    p->pfnVideoProcessorSetStreamDestRect             = ddi11_1VideoProcessorSetStreamDestRect;
    p->pfnVideoProcessorSetStreamAlpha                = ddi11_1VideoProcessorSetStreamAlpha;
    p->pfnVideoProcessorSetStreamPalette              = ddi11_1VideoProcessorSetStreamPalette;
    p->pfnVideoProcessorSetStreamPixelAspectRatio     = ddi11_1VideoProcessorSetStreamPixelAspectRatio;
    p->pfnVideoProcessorSetStreamLumaKey              = ddi11_1VideoProcessorSetStreamLumaKey;
    p->pfnVideoProcessorSetStreamStereoFormat         = ddi11_1VideoProcessorSetStreamStereoFormat;
    p->pfnVideoProcessorSetStreamAutoProcessingMode   = ddi11_1VideoProcessorSetStreamAutoProcessingMode;
    p->pfnVideoProcessorSetStreamFilter               = ddi11_1VideoProcessorSetStreamFilter;
    p->pfnVideoProcessorSetStreamExtension            = ddi11_1VideoProcessorSetStreamExtension;
    p->pfnVideoProcessorGetStreamExtension            = ddi11_1VideoProcessorGetStreamExtension;
    p->pfnVideoProcessorBlt                           = ddi11_1VideoProcessorBlt;
    p->pfnCalcPrivateVideoDecoderOutputViewSize       = ddi11_1CalcPrivateVideoDecoderOutputViewSize;
    p->pfnCreateVideoDecoderOutputView                = ddi11_1CreateVideoDecoderOutputView;
    p->pfnDestroyVideoDecoderOutputView               = ddi11_1DestroyVideoDecoderOutputView;
    p->pfnCalcPrivateVideoProcessorInputViewSize      = ddi11_1CalcPrivateVideoProcessorInputViewSize;
    p->pfnCreateVideoProcessorInputView               = ddi11_1CreateVideoProcessorInputView;
    p->pfnDestroyVideoProcessorInputView              = ddi11_1DestroyVideoProcessorInputView;
    p->pfnCalcPrivateVideoProcessorOutputViewSize     = ddi11_1CalcPrivateVideoProcessorOutputViewSize;
    p->pfnCreateVideoProcessorOutputView              = ddi11_1CreateVideoProcessorOutputView;
    p->pfnDestroyVideoProcessorOutputView             = ddi11_1DestroyVideoProcessorOutputView;
    p->pfnVideoProcessorInputViewReadAfterWriteHazard = ddi11_1VideoProcessorInputViewReadAfterWriteHazard;
    p->pfnGetContentProtectionCaps                    = ddi11_1GetContentProtectionCaps;
    p->pfnGetCryptoKeyExchangeType                    = ddi11_1GetCryptoKeyExchangeType;
    p->pfnCalcPrivateCryptoSessionSize                = ddi11_1CalcPrivateCryptoSessionSize;
    p->pfnCreateCryptoSession                         = ddi11_1CreateCryptoSession;
    p->pfnDestroyCryptoSession                        = ddi11_1DestroyCryptoSession;
    p->pfnGetCertificateSize                          = ddi11_1GetCertificateSize;
    p->pfnGetCertificate                              = ddi11_1GetCertificate;
    p->pfnNegotiateCryptoSessionKeyExchange           = ddi11_1NegotiateCryptoSessionKeyExchange;
    p->pfnEncryptionBlt                               = ddi11_1EncryptionBlt;
    p->pfnDecryptionBlt                               = ddi11_1DecryptionBlt;
    p->pfnStartSessionKeyRefresh                      = ddi11_1StartSessionKeyRefresh;
    p->pfnFinishSessionKeyRefresh                     = ddi11_1FinishSessionKeyRefresh;
    p->pfnGetEncryptionBltKey                         = ddi11_1GetEncryptionBltKey;
    p->pfnCalcPrivateAuthenticatedChannelSize         = ddi11_1CalcPrivateAuthenticatedChannelSize;
    p->pfnCreateAuthenticatedChannel                  = ddi11_1CreateAuthenticatedChannel;
    p->pfnDestroyAuthenticatedChannel                 = ddi11_1DestroyAuthenticatedChannel;
    p->pfnNegotiateAuthenticatedChannelKeyExchange    = ddi11_1NegotiateAuthenticatedChannelKeyExchange;
    p->pfnQueryAuthenticatedChannel                   = ddi11_1QueryAuthenticatedChannel;
    p->pfnConfigureAuthenticatedChannel               = ddi11_1ConfigureAuthenticatedChannel;
    p->pfnVideoDecoderGetHandle                       = ddi11_1VideoDecoderGetHandle;
    p->pfnCryptoSessionGetHandle                      = ddi11_1CryptoSessionGetHandle;
    p->pfnVideoProcessorSetStreamRotation             = ddi11_1VideoProcessorSetStreamRotation;
    p->pfnGetCaptureHandle                            = ddi11_1GetCaptureHandle;

    return S_OK;
}
