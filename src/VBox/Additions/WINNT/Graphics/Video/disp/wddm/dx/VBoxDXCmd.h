/* $Id: VBoxDXCmd.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxVideo Display D3D User mode dll
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_dx_VBoxDXCmd_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_dx_VBoxDXCmd_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

int vgpu10DefineBlendState(PVBOXDX_DEVICE pDevice,
                           SVGA3dBlendStateId blendId,
                           uint8 alphaToCoverageEnable,
                           uint8 independentBlendEnable,
                           const SVGA3dDXBlendStatePerRT *perRT);
int vgpu10DestroyBlendState(PVBOXDX_DEVICE pDevice,
                            SVGA3dBlendStateId blendId);
int vgpu10DefineDepthStencilState(PVBOXDX_DEVICE pDevice,
                                  SVGA3dDepthStencilStateId depthStencilId,
                                  uint8_t depthEnable,
                                  SVGA3dDepthWriteMask depthWriteMask,
                                  SVGA3dComparisonFunc depthFunc,
                                  uint8 stencilEnable,
                                  uint8 frontEnable,
                                  uint8 backEnable,
                                  uint8 stencilReadMask,
                                  uint8 stencilWriteMask,
                                  uint8 frontStencilFailOp,
                                  uint8 frontStencilDepthFailOp,
                                  uint8 frontStencilPassOp,
                                  SVGA3dComparisonFunc frontStencilFunc,
                                  uint8 backStencilFailOp,
                                  uint8 backStencilDepthFailOp,
                                  uint8 backStencilPassOp,
                                  SVGA3dComparisonFunc backStencilFunc);
int vgpu10DestroyDepthStencilState(PVBOXDX_DEVICE pDevice,
                                   SVGA3dDepthStencilStateId depthStencilId);
int vgpu10DefineRasterizerState(PVBOXDX_DEVICE pDevice,
                                SVGA3dRasterizerStateId rasterizerId,
                                uint8 fillMode,
                                SVGA3dCullMode cullMode,
                                uint8 frontCounterClockwise,
                                uint8 provokingVertexLast,
                                int32 depthBias,
                                float depthBiasClamp,
                                float slopeScaledDepthBias,
                                uint8 depthClipEnable,
                                uint8 scissorEnable,
                                SVGA3dMultisampleRastEnable multisampleEnable,
                                uint8 antialiasedLineEnable,
                                float lineWidth,
                                uint8 lineStippleEnable,
                                uint8 lineStippleFactor,
                                uint16 lineStipplePattern);
int vgpu10DefineRasterizerState_v2(PVBOXDX_DEVICE pDevice,
                                   SVGA3dRasterizerStateId rasterizerId,
                                   uint8 fillMode,
                                   SVGA3dCullMode cullMode,
                                   uint8 frontCounterClockwise,
                                   uint8 provokingVertexLast,
                                   int32 depthBias,
                                   float depthBiasClamp,
                                   float slopeScaledDepthBias,
                                   uint8 depthClipEnable,
                                   uint8 scissorEnable,
                                   SVGA3dMultisampleRastEnable multisampleEnable,
                                   uint8 antialiasedLineEnable,
                                   float lineWidth,
                                   uint8 lineStippleEnable,
                                   uint8 lineStippleFactor,
                                   uint16 lineStipplePattern,
                                   uint32 forcedSampleCount);
int vgpu10DestroyRasterizerState(PVBOXDX_DEVICE pDevice,
                                 SVGA3dRasterizerStateId rasterizerId);
int vgpu10DefineSamplerState(PVBOXDX_DEVICE pDevice,
                             SVGA3dSamplerId samplerId,
                             SVGA3dFilter filter,
                             uint8 addressU,
                             uint8 addressV,
                             uint8 addressW,
                             float mipLODBias,
                             uint8 maxAnisotropy,
                             SVGA3dComparisonFunc comparisonFunc,
                             SVGA3dRGBAFloat borderColor,
                             float minLOD,
                             float maxLOD);
int vgpu10DestroySamplerState(PVBOXDX_DEVICE pDevice,
                              SVGA3dSamplerId samplerId);
int vgpu10DefineElementLayout(PVBOXDX_DEVICE pDevice,
                              SVGA3dElementLayoutId elementLayoutId,
                              uint32_t cElements,
                              SVGA3dInputElementDesc *paDesc);
int vgpu10DestroyElementLayout(PVBOXDX_DEVICE pDevice,
                               SVGA3dElementLayoutId elementLayoutId);
int vgpu10SetInputLayout(PVBOXDX_DEVICE pDevice,
                         SVGA3dElementLayoutId elementLayoutId);
int vgpu10SetBlendState(PVBOXDX_DEVICE pDevice,
                        SVGA3dBlendStateId blendId,
                        const float blendFactor[4],
                        uint32 sampleMask);
int vgpu10SetDepthStencilState(PVBOXDX_DEVICE pDevice,
                               SVGA3dDepthStencilStateId depthStencilId,
                               uint32 stencilRef);
int vgpu10SetRasterizerState(PVBOXDX_DEVICE pDevice,
                             SVGA3dRasterizerStateId rasterizerId);
int vgpu10SetSamplers(PVBOXDX_DEVICE pDevice,
                      uint32 startSampler,
                      SVGA3dShaderType type,
                      uint32_t numSamplers,
                      const SVGA3dSamplerId *paSamplerIds);
int vgpu10SetTopology(PVBOXDX_DEVICE pDevice,
                      SVGA3dPrimitiveType topology);
int vgpu10Draw(PVBOXDX_DEVICE pDevice,
               uint32 vertexCount,
               uint32 startVertexLocation);
int vgpu10DrawIndexed(PVBOXDX_DEVICE pDevice,
                      uint32 indexCount,
                      uint32 startIndexLocation,
                      int32 baseVertexLocation);
int vgpu10DrawInstanced(PVBOXDX_DEVICE pDevice,
                        uint32 vertexCountPerInstance,
                        uint32 instanceCount,
                        uint32 startVertexLocation,
                        uint32 startInstanceLocation);
int vgpu10DrawIndexedInstanced(PVBOXDX_DEVICE pDevice,
                               uint32 indexCountPerInstance,
                               uint32 instanceCount,
                               uint32 startIndexLocation,
                               int32 baseVertexLocation,
                               uint32 startInstanceLocation);
int vgpu10DrawAuto(PVBOXDX_DEVICE pDevice);
int vgpu10SetViewports(PVBOXDX_DEVICE pDevice,
                       uint32_t cViewports,
                       const D3D10_DDI_VIEWPORT *paViewports);
int vgpu10SetScissorRects(PVBOXDX_DEVICE pDevice,
                          uint32_t cRects,
                          const D3D10_DDI_RECT *paRects);
int vgpu10DefineShader(PVBOXDX_DEVICE pDevice,
                       SVGA3dShaderId shaderId,
                       SVGA3dShaderType type,
                       uint32_t sizeInBytes);
int vgpu10DefineStreamOutputWithMob(PVBOXDX_DEVICE pDevice,
                                    SVGA3dStreamOutputId soid,
                                    uint32 numOutputStreamEntries,
                                    uint32 numOutputStreamStrides,
                                    const uint32 *streamOutputStrideInBytes,
                                    uint32 rasterizedStream);
int vgpu10BindStreamOutput(PVBOXDX_DEVICE pDevice,
                           SVGA3dStreamOutputId soid,
                           D3DKMT_HANDLE hAllocation,
                           uint32 offsetInBytes,
                           uint32 sizeInBytes);
int vgpu10SetStreamOutput(PVBOXDX_DEVICE pDevice,
                          SVGA3dStreamOutputId soid);
int vgpu10DestroyShader(PVBOXDX_DEVICE pDevice,
                        SVGA3dShaderId shaderId);
int vgpu10BindShader(PVBOXDX_DEVICE pDevice,
                     uint32_t shid,
                     D3DKMT_HANDLE hAllocation,
                     uint32_t offsetInBytes);
int vgpu10SetShader(PVBOXDX_DEVICE pDevice,
                    SVGA3dShaderId shaderId,
                    SVGA3dShaderType type);
int vgpu10SetVertexBuffers(PVBOXDX_DEVICE pDevice,
                           uint32_t startBuffer,
                           uint32_t numBuffers,
                           D3DKMT_HANDLE *paAllocations,
                           const UINT *paStrides,
                           const UINT *paOffsets);
int vgpu10SetIndexBuffer(PVBOXDX_DEVICE pDevice,
                         D3DKMT_HANDLE hAllocation,
                         SVGA3dSurfaceFormat format,
                         uint32_t offset);
int vgpu10SoSetTargets(PVBOXDX_DEVICE pDevice,
                       uint32_t numTargets,
                       D3DKMT_HANDLE *paAllocations,
                       uint32_t *paOffsets,
                       uint32_t *paSizes);
int vgpu10DefineShaderResourceView(PVBOXDX_DEVICE pDevice,
                                   SVGA3dShaderResourceViewId shaderResourceViewId,
                                   D3DKMT_HANDLE hAllocation,
                                   SVGA3dSurfaceFormat format,
                                   SVGA3dResourceType resourceDimension,
                                   SVGA3dShaderResourceViewDesc const *pDesc);
int vgpu10GenMips(PVBOXDX_DEVICE pDevice,
                  SVGA3dShaderResourceViewId shaderResourceViewId);
int vgpu10DestroyShaderResourceView(PVBOXDX_DEVICE pDevice,
                                    SVGA3dShaderResourceViewId shaderResourceViewId);
int vgpu10DefineRenderTargetView(PVBOXDX_DEVICE pDevice,
                                 SVGA3dRenderTargetViewId renderTargetViewId,
                                 D3DKMT_HANDLE hAllocation,
                                 SVGA3dSurfaceFormat format,
                                 SVGA3dResourceType resourceDimension,
                                 SVGA3dRenderTargetViewDesc const *pDesc);
int vgpu10ClearRenderTargetView(PVBOXDX_DEVICE pDevice,
                                SVGA3dRenderTargetViewId renderTargetViewId,
                                const float rgba[4]);
int vgpu10DestroyRenderTargetView(PVBOXDX_DEVICE pDevice,
                                  SVGA3dRenderTargetViewId renderTargetViewId);
int vgpu10DefineDepthStencilView(PVBOXDX_DEVICE pDevice,
                                 SVGA3dDepthStencilViewId depthStencilViewId,
                                 D3DKMT_HANDLE hAllocation,
                                 SVGA3dSurfaceFormat format,
                                 SVGA3dResourceType resourceDimension,
                                 uint32 mipSlice,
                                 uint32 firstArraySlice,
                                 uint32 arraySize,
                                 SVGA3DCreateDSViewFlags flags);
int vgpu10ClearDepthStencilView(PVBOXDX_DEVICE pDevice,
                                uint16 flags,
                                uint16 stencil,
                                SVGA3dDepthStencilViewId depthStencilViewId,
                                float depth);
int vgpu10DestroyDepthStencilView(PVBOXDX_DEVICE pDevice,
                                  SVGA3dDepthStencilViewId depthStencilViewId);
int vgpu10SetRenderTargets(PVBOXDX_DEVICE pDevice,
                           SVGA3dDepthStencilViewId depthStencilViewId,
                           uint32_t numRTVs,
                           uint32_t numClearSlots,
                           uint32_t *paRenderTargetViewIds);
int vgpu10SetShaderResources(PVBOXDX_DEVICE pDevice,
                             SVGA3dShaderType type,
                             uint32 startView,
                             uint32_t numViews,
                             uint32_t *paViewIds);
int vgpu10SetSingleConstantBuffer(PVBOXDX_DEVICE pDevice,
                                  uint32 slot,
                                  SVGA3dShaderType type,
                                  D3DKMT_HANDLE hAllocation,
                                  uint32 offsetInBytes,
                                  uint32 sizeInBytes);
int vgpu10UpdateSubResource(PVBOXDX_DEVICE pDevice,
                            D3DKMT_HANDLE hAllocation,
                            uint32 subResource,
                            const SVGA3dBox *pBox);
int vgpu10ReadbackSubResource(PVBOXDX_DEVICE pDevice,
                            D3DKMT_HANDLE hAllocation,
                            uint32 subResource);
int vgpu10TransferFromBuffer(PVBOXDX_DEVICE pDevice,
                             D3DKMT_HANDLE hSrcAllocation,
                             uint32 srcOffset,
                             uint32 srcPitch,
                             uint32 srcSlicePitch,
                             D3DKMT_HANDLE hDstAllocation,
                             uint32 destSubResource,
                             SVGA3dBox const &destBox);
int vgpu10ResourceCopyRegion(PVBOXDX_DEVICE pDevice,
                             D3DKMT_HANDLE hDstAllocation,
                             uint32 dstSubResource,
                             uint32 dstX,
                             uint32 dstY,
                             uint32 dstZ,
                             D3DKMT_HANDLE hSrcAllocation,
                             uint32 srcSubResource,
                             SVGA3dBox const &srcBox);
int vgpu10ResourceCopy(PVBOXDX_DEVICE pDevice,
                       D3DKMT_HANDLE hDstAllocation,
                       D3DKMT_HANDLE hSrcAllocation);
int vgpu10ResolveCopy(PVBOXDX_DEVICE pDevice,
                      D3DKMT_HANDLE hDstAllocation,
                      uint32 dstSubResource,
                      D3DKMT_HANDLE hSrcAllocation,
                      uint32 srcSubResource,
                      SVGA3dSurfaceFormat copyFormat);
int vgpu10MobFence64(PVBOXDX_DEVICE pDevice,
                     uint64 value,
                     D3DKMT_HANDLE hAllocation,
                     uint32 mobOffset);
int vgpu10DefineQuery(PVBOXDX_DEVICE pDevice,
                      SVGA3dQueryId queryId,
                      SVGA3dQueryType type,
                      SVGA3dDXQueryFlags flags);
int vgpu10DestroyQuery(PVBOXDX_DEVICE pDevice,
                       SVGA3dQueryId queryId);
int vgpu10BindQuery(PVBOXDX_DEVICE pDevice,
                    SVGA3dQueryId queryId,
                    D3DKMT_HANDLE hAllocation);
int vgpu10SetQueryOffset(PVBOXDX_DEVICE pDevice,
                         SVGA3dQueryId queryId,
                         uint32 mobOffset);
int vgpu10BeginQuery(PVBOXDX_DEVICE pDevice,
                     SVGA3dQueryId queryId);
int vgpu10EndQuery(PVBOXDX_DEVICE pDevice,
                   SVGA3dQueryId queryId);
int vgpu10ReadbackQuery(PVBOXDX_DEVICE pDevice,
                        SVGA3dQueryId queryId);
int vgpu10SetPredication(PVBOXDX_DEVICE pDevice,
                         SVGA3dQueryId queryId,
                         uint32 predicateValue);
int vgpu10DefineUAView(PVBOXDX_DEVICE pDevice,
                       SVGA3dUAViewId uaViewId,
                       D3DKMT_HANDLE hAllocation,
                       SVGA3dSurfaceFormat format,
                       SVGA3dResourceType resourceDimension,
                       const SVGA3dUAViewDesc &desc);
int vgpu10DestroyUAView(PVBOXDX_DEVICE pDevice,
                        SVGA3dUAViewId uaViewId);
int vgpu10ClearUAViewUint(PVBOXDX_DEVICE pDevice,
                          SVGA3dUAViewId uaViewId,
                          const uint32 value[4]);
int vgpu10ClearUAViewFloat(PVBOXDX_DEVICE pDevice,
                           SVGA3dUAViewId uaViewId,
                           const float value[4]);
int vgpu10SetCSUAViews(PVBOXDX_DEVICE pDevice,
                       uint32 startIndex,
                       uint32 numViews,
                       const SVGA3dUAViewId *paViewIds);
int vgpu10SetUAViews(PVBOXDX_DEVICE pDevice,
                     uint32 uavSpliceIndex,
                     uint32 numViews,
                     const SVGA3dUAViewId *paViewIds);
int vgpu10SetStructureCount(PVBOXDX_DEVICE pDevice,
                            SVGA3dUAViewId uaViewId,
                            uint32 structureCount);
int vgpu10Dispatch(PVBOXDX_DEVICE pDevice,
                   uint32 threadGroupCountX,
                   uint32 threadGroupCountY,
                   uint32 threadGroupCountZ);
int vgpu10DispatchIndirect(PVBOXDX_DEVICE pDevice,
                           D3DKMT_HANDLE hAllocation,
                           uint32 byteOffsetForArgs);
int vgpu10DrawIndexedInstancedIndirect(PVBOXDX_DEVICE pDevice,
                                       D3DKMT_HANDLE hAllocation,
                                       uint32 byteOffsetForArgs);
int vgpu10DrawInstancedIndirect(PVBOXDX_DEVICE pDevice,
                                D3DKMT_HANDLE hAllocation,
                                uint32 byteOffsetForArgs);
int vgpu10CopyStructureCount(PVBOXDX_DEVICE pDevice,
                             SVGA3dUAViewId srcUAViewId,
                             D3DKMT_HANDLE hDstBuffer,
                             uint32 destByteOffset);
int vgpu10ClearRenderTargetViewRegion(PVBOXDX_DEVICE pDevice,
                                      SVGA3dRenderTargetViewId viewId,
                                      const float color[4],
                                      const D3D10_DDI_RECT *paRects,
                                      uint32_t cRects);
int vgpu10PresentBlt(PVBOXDX_DEVICE pDevice,
                     D3DKMT_HANDLE hSrcAllocation,
                     uint32 srcSubResource,
                     D3DKMT_HANDLE hDstAllocation,
                     uint32 destSubResource,
                     SVGA3dBox const &boxSrc,
                     SVGA3dBox const &boxDest,
                     SVGA3dDXPresentBltMode mode);

int vgpu10DefineVideoProcessor(PVBOXDX_DEVICE pDevice,
                               uint32 uVideoProcessorId,
                               VBSVGA3dVideoProcessorDesc const &desc);
int vgpu10DefineVideoDecoderOutputView(PVBOXDX_DEVICE pDevice,
                                       VBSVGA3dVideoDecoderOutputViewId videoDecoderOutputViewId,
                                       D3DKMT_HANDLE hAllocation,
                                       VBSVGA3dVDOVDesc const &desc);
int vgpu10DefineVideoDecoder(PVBOXDX_DEVICE pDevice,
                             VBSVGA3dVideoDecoderId videoDecoderId,
                             VBSVGA3dVideoDecoderDesc const &desc,
                             VBSVGA3dVideoDecoderConfig const &config);
int vgpu10VideoDecoderBeginFrame(PVBOXDX_DEVICE pDevice,
                                 VBSVGA3dVideoDecoderId videoDecoderId,
                                 VBSVGA3dVideoDecoderOutputViewId videoDecoderOutputViewId);
int vgpu10VideoDecoderSubmitBuffers(PVBOXDX_DEVICE pDevice,
                                    VBSVGA3dVideoDecoderId videoDecoderId,
                                    uint32 bufferCount,
                                    D3DKMT_HANDLE const *pahAllocation,
                                    VBSVGA3dVideoDecoderBufferDesc const *paBufferDesc);
int vgpu10VideoDecoderEndFrame(PVBOXDX_DEVICE pDevice,
                               VBSVGA3dVideoDecoderId videoDecoderId);
int vgpu10DefineVideoProcessorInputView(PVBOXDX_DEVICE pDevice,
                                        VBSVGA3dVideoProcessorInputViewId videoProcessorInputViewId,
                                        D3DKMT_HANDLE hAllocation,
                                        VBSVGA3dVideoProcessorDesc const &contentDesc,
                                        VBSVGA3dVPIVDesc const &desc);
int vgpu10DefineVideoProcessorOutputView(PVBOXDX_DEVICE pDevice,
                                         VBSVGA3dVideoProcessorOutputViewId videoProcessorOutputViewId,
                                         D3DKMT_HANDLE hAllocation,
                                         VBSVGA3dVideoProcessorDesc const &contentDesc,
                                         VBSVGA3dVPOVDesc const &desc);
int vgpu10VideoProcessorBlt(PVBOXDX_DEVICE pDevice,
                            VBSVGA3dVideoProcessorId videoProcessorId,
                            VBSVGA3dVideoProcessorOutputViewId videoProcessorOutputViewId,
                            uint32 OutputFrame,
                            uint32 StreamCount,
                            uint32 cbVideoProcessorStreams,
                            VBSVGA3dVideoProcessorStream *paVideoProcessorStreams);
int vgpu10DestroyVideoDecoder(PVBOXDX_DEVICE pDevice,
                              VBSVGA3dVideoDecoderId videoDecoderId);
int vgpu10DestroyVideoDecoderOutputView(PVBOXDX_DEVICE pDevice,
                                        VBSVGA3dVideoDecoderOutputViewId videoDecoderOutputViewId);
int vgpu10DestroyVideoProcessor(PVBOXDX_DEVICE pDevice,
                                VBSVGA3dVideoProcessorId videoProcessorId);
int vgpu10DestroyVideoProcessorInputView(PVBOXDX_DEVICE pDevice,
                                         VBSVGA3dVideoProcessorInputViewId videoProcessorInputViewId);
int vgpu10DestroyVideoProcessorOutputView(PVBOXDX_DEVICE pDevice,
                                          VBSVGA3dVideoProcessorOutputViewId videoProcessorOutputViewId);
int vgpu10VideoProcessorSetOutputTargetRect(PVBOXDX_DEVICE pDevice,
                                            VBSVGA3dVideoProcessorId videoProcessorId,
                                            BOOL enable,
                                            RECT const &outputRect);
int vgpu10VideoProcessorSetOutputBackgroundColor(PVBOXDX_DEVICE pDevice,
                                                 VBSVGA3dVideoProcessorId videoProcessorId,
                                                 BOOL ycbcr,
                                                 D3D11_1DDI_VIDEO_COLOR const &color);
int vgpu10VideoProcessorSetOutputColorSpace(PVBOXDX_DEVICE pDevice,
                                            VBSVGA3dVideoProcessorId videoProcessorId,
                                            VBSVGA3dVideoProcessorColorSpace const &colorSpace);
int vgpu10VideoProcessorSetOutputAlphaFillMode(PVBOXDX_DEVICE pDevice,
                                               VBSVGA3dVideoProcessorId videoProcessorId,
                                               VBSVGA3dVideoProcessorAlphaFillMode fillMode,
                                               uint32 streamIndex);
int vgpu10VideoProcessorSetOutputConstriction(PVBOXDX_DEVICE pDevice,
                                               VBSVGA3dVideoProcessorId videoProcessorId,
                                               BOOL enabled,
                                               SIZE constrictonSize);
int vgpu10VideoProcessorSetOutputStereoMode(PVBOXDX_DEVICE pDevice,
                                            VBSVGA3dVideoProcessorId videoProcessorId,
                                            BOOL enable);
int vgpu10VideoProcessorSetStreamFrameFormat(PVBOXDX_DEVICE pDevice,
                                             VBSVGA3dVideoProcessorId videoProcessorId,
                                             uint32 streamIndex,
                                             VBSVGA3dVideoFrameFormat format);
int vgpu10VideoProcessorSetStreamColorSpace(PVBOXDX_DEVICE pDevice,
                                            VBSVGA3dVideoProcessorId videoProcessorId,
                                            uint32 streamIndex,
                                            VBSVGA3dVideoProcessorColorSpace const &colorSpace);
int vgpu10VideoProcessorSetStreamOutputRate(PVBOXDX_DEVICE pDevice,
                                            VBSVGA3dVideoProcessorId videoProcessorId,
                                            uint32 streamIndex,
                                            VBSVGA3dVideoProcessorOutputRate outputRate,
                                            uint8 repeatFrame,
                                            SVGA3dFraction64 const &customRate);
int vgpu10VideoProcessorSetStreamSourceRect(PVBOXDX_DEVICE pDevice,
                                            VBSVGA3dVideoProcessorId videoProcessorId,
                                            uint32 streamIndex,
                                            BOOL enable,
                                            RECT const *pSourceRect);
int vgpu10VideoProcessorSetStreamDestRect(PVBOXDX_DEVICE pDevice,
                                          VBSVGA3dVideoProcessorId videoProcessorId,
                                          uint32 streamIndex,
                                          BOOL enable,
                                          RECT const *pDestRect);
int vgpu10VideoProcessorSetStreamAlpha(PVBOXDX_DEVICE pDevice,
                                       VBSVGA3dVideoProcessorId videoProcessorId,
                                       uint32 streamIndex,
                                       BOOL enable,
                                       float alpha);
int vgpu10VideoProcessorSetStreamPalette(PVBOXDX_DEVICE pDevice,
                                         VBSVGA3dVideoProcessorId videoProcessorId,
                                         uint32 streamIndex,
                                         UINT Count,
                                         UINT const *pEntries);
int vgpu10VideoProcessorSetStreamPixelAspectRatio(PVBOXDX_DEVICE pDevice,
                                                  VBSVGA3dVideoProcessorId videoProcessorId,
                                                  uint32 streamIndex,
                                                  BOOL enable,
                                                  SVGA3dFraction64 const &sourceRatio,
                                                  SVGA3dFraction64 const &destRatio);
int vgpu10VideoProcessorSetStreamLumaKey(PVBOXDX_DEVICE pDevice,
                                         VBSVGA3dVideoProcessorId videoProcessorId,
                                         uint32 streamIndex,
                                         BOOL enable,
                                         float lower,
                                         float upper);
int vgpu10VideoProcessorSetStreamStereoFormat(PVBOXDX_DEVICE pDevice,
                                              VBSVGA3dVideoProcessorId videoProcessorId,
                                              uint32 streamIndex,
                                              BOOL enable,
                                              VBSVGA3dVideoProcessorStereoFormat stereoFormat,
                                              uint8 leftViewFrame0,
                                              uint8 baseViewFrame0,
                                              VBSVGA3dVideoProcessorStereoFlipMode flipMode,
                                              int monoOffset);
int vgpu10VideoProcessorSetStreamAutoProcessingMode(PVBOXDX_DEVICE pDevice,
                                                    VBSVGA3dVideoProcessorId videoProcessorId,
                                                    uint32 streamIndex,
                                                    BOOL enable);
int vgpu10VideoProcessorSetStreamFilter(PVBOXDX_DEVICE pDevice,
                                        VBSVGA3dVideoProcessorId videoProcessorId,
                                        uint32 streamIndex,
                                        BOOL enable,
                                        VBSVGA3dVideoProcessorFilter filter,
                                        int level);
int vgpu10VideoProcessorSetStreamRotation(PVBOXDX_DEVICE pDevice,
                                          VBSVGA3dVideoProcessorId videoProcessorId,
                                          uint32 streamIndex,
                                          BOOL enable,
                                          VBSVGA3dVideoProcessorRotation rotation);
int vgpu10GetVideoCapability(PVBOXDX_DEVICE pDevice,
                             VBSVGA3dVideoCapability capability,
                             D3DKMT_HANDLE hAllocation,
                             uint32 offsetInBytes,
                             uint32 sizeInBytes,
                             uint64 fenceValue);
int vgpu10ClearView(PVBOXDX_DEVICE pDevice,
                    SVGAFifo3dCmdId cmdId,
                    uint32_t viewId,
                    const float color[4],
                    const D3D10_DDI_RECT *paRects,
                    uint32_t cRects);

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_dx_VBoxDXCmd_h */
