/* $Id: GaDdi.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - Gallium WDDM user mode driver functions.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_GaDdi_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_GaDdi_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <d3dumddi.h>

/*
 * PFND3DDDI_* functions implemented for the Gallium based driver.
 */
HRESULT APIENTRY GaDdiSetRenderState(HANDLE hDevice, const D3DDDIARG_RENDERSTATE *pData);
HRESULT APIENTRY GaDdiUpdateWInfo(HANDLE hDevice, const D3DDDIARG_WINFO *pData);
HRESULT APIENTRY GaDdiValidateDevice(HANDLE hDevice, D3DDDIARG_VALIDATETEXTURESTAGESTATE *pData);
HRESULT APIENTRY GaDdiSetTextureStageState(HANDLE hDevice, const D3DDDIARG_TEXTURESTAGESTATE *pData);
HRESULT APIENTRY GaDdiSetTexture(HANDLE hDevice, UINT Stage, HANDLE hTexture);
HRESULT APIENTRY GaDdiSetPixelShader(HANDLE hDevice, HANDLE hShaderHandle);
HRESULT APIENTRY GaDdiSetPixelShaderConst(HANDLE hDevice, const D3DDDIARG_SETPIXELSHADERCONST *pData,
                                          const FLOAT *pRegisters);
HRESULT APIENTRY GaDdiSetStreamSourceUm(HANDLE hDevice, const D3DDDIARG_SETSTREAMSOURCEUM *pData,
                                        const VOID *pUMBuffer);
HRESULT APIENTRY GaDdiSetIndices(HANDLE hDevice, const D3DDDIARG_SETINDICES *pData);
HRESULT APIENTRY GaDdiSetIndicesUm(HANDLE hDevice, UINT IndexSize, const VOID *pUMBuffer);
HRESULT APIENTRY GaDdiDrawPrimitive(HANDLE hDevice, const D3DDDIARG_DRAWPRIMITIVE *pData, const UINT *pFlagBuffer);
HRESULT APIENTRY GaDdiDrawIndexedPrimitive(HANDLE hDevice, const D3DDDIARG_DRAWINDEXEDPRIMITIVE *pData);
HRESULT APIENTRY GaDdiDrawRectPatch(HANDLE hDevice, const D3DDDIARG_DRAWRECTPATCH *pData,
                                    const D3DDDIRECTPATCH_INFO *pInfo, const FLOAT *pPatch);
HRESULT APIENTRY GaDdiDrawTriPatch(HANDLE hDevice, const D3DDDIARG_DRAWTRIPATCH *pData,
                                   const D3DDDITRIPATCH_INFO *pInfo, const FLOAT *pPatch);
HRESULT APIENTRY GaDdiDrawPrimitive2(HANDLE hDevice, const D3DDDIARG_DRAWPRIMITIVE2 *pData);
HRESULT APIENTRY GaDdiDrawIndexedPrimitive2(HANDLE hDevice, const D3DDDIARG_DRAWINDEXEDPRIMITIVE2 *pData,
                                            UINT dwIndicesSize, const VOID *pIndexBuffer, const UINT *pFlagBuffer);
HRESULT APIENTRY GaDdiVolBlt(HANDLE hDevice, const D3DDDIARG_VOLUMEBLT *pData);
HRESULT APIENTRY GaDdiBufBlt(HANDLE hDevice, const D3DDDIARG_BUFFERBLT *pData);
HRESULT APIENTRY GaDdiTexBlt(HANDLE hDevice, const D3DDDIARG_TEXBLT *pData);
HRESULT APIENTRY GaDdiStateSet(HANDLE hDevice, D3DDDIARG_STATESET *pData);
HRESULT APIENTRY GaDdiSetPriority(HANDLE hDevice, const D3DDDIARG_SETPRIORITY *pData);
HRESULT APIENTRY GaDdiClear(HANDLE hDevice, const D3DDDIARG_CLEAR *pData, UINT NumRect, const RECT *pRect);
HRESULT APIENTRY GaDdiUpdatePalette(HANDLE hDevice, const D3DDDIARG_UPDATEPALETTE *pData,
                                    const PALETTEENTRY *pPaletteData);
HRESULT APIENTRY GaDdiSetPalette(HANDLE hDevice, const D3DDDIARG_SETPALETTE *pData);
HRESULT APIENTRY GaDdiSetVertexShaderConst(HANDLE hDevice, const D3DDDIARG_SETVERTEXSHADERCONST *pData,
                                           const VOID *pRegisters);
HRESULT APIENTRY GaDdiMultiplyTransform(HANDLE hDevice, const D3DDDIARG_MULTIPLYTRANSFORM *pData);
HRESULT APIENTRY GaDdiSetTransform(HANDLE hDevice, const D3DDDIARG_SETTRANSFORM *pData);
HRESULT APIENTRY GaDdiSetViewport(HANDLE hDevice, const D3DDDIARG_VIEWPORTINFO *pData);
HRESULT APIENTRY GaDdiSetZRange(HANDLE hDevice, const D3DDDIARG_ZRANGE *pData);
HRESULT APIENTRY GaDdiSetMaterial(HANDLE hDevice, const D3DDDIARG_SETMATERIAL *pData);
HRESULT APIENTRY GaDdiSetLight(HANDLE hDevice, const D3DDDIARG_SETLIGHT *pData, const D3DDDI_LIGHT *pLightProperties);
HRESULT APIENTRY GaDdiCreateLight(HANDLE hDevice, const D3DDDIARG_CREATELIGHT *pData);
HRESULT APIENTRY GaDdiDestroyLight(HANDLE hDevice, const D3DDDIARG_DESTROYLIGHT *pData);
HRESULT APIENTRY GaDdiSetClipPlane(HANDLE hDevice, const D3DDDIARG_SETCLIPPLANE *pData);
HRESULT APIENTRY GaDdiGetInfo(HANDLE hDevice, UINT DevInfoID, VOID *pDevInfoStruct, UINT DevInfoSize);
HRESULT APIENTRY GaDdiLock(HANDLE hDevice, D3DDDIARG_LOCK *pData);
HRESULT APIENTRY GaDdiUnlock(HANDLE hDevice, const D3DDDIARG_UNLOCK *pData);
HRESULT APIENTRY GaDdiCreateResource(HANDLE hDevice, D3DDDIARG_CREATERESOURCE *pResource);
HRESULT APIENTRY GaDdiDestroyResource(HANDLE hDevice, HANDLE hResource);
HRESULT APIENTRY GaDdiSetDisplayMode(HANDLE hDevice, const D3DDDIARG_SETDISPLAYMODE *pData);
HRESULT APIENTRY GaDdiPresent(HANDLE hDevice, const D3DDDIARG_PRESENT *pData);
HRESULT APIENTRY GaDdiFlush(HANDLE hDevice);
HRESULT APIENTRY GaDdiCreateVertexShaderFunc(HANDLE hDevice, D3DDDIARG_CREATEVERTEXSHADERFUNC *pData, const UINT *pCode);
HRESULT APIENTRY GaDdiDeleteVertexShaderFunc(HANDLE hDevice, HANDLE hShaderHandle);
HRESULT APIENTRY GaDdiSetVertexShaderFunc(HANDLE hDevice, HANDLE hShaderHandle);
HRESULT APIENTRY GaDdiCreateVertexShaderDecl(HANDLE hDevice, D3DDDIARG_CREATEVERTEXSHADERDECL *pData,
                                             const D3DDDIVERTEXELEMENT *pVertexElements);
HRESULT APIENTRY GaDdiDeleteVertexShaderDecl(HANDLE hDevice, HANDLE hShaderHandle);
HRESULT APIENTRY GaDdiSetVertexShaderDecl(HANDLE hDevice, HANDLE hShaderHandle);
HRESULT APIENTRY GaDdiSetVertexShaderConstI(HANDLE hDevice, const D3DDDIARG_SETVERTEXSHADERCONSTI *pData,
                                            const INT *pRegisters);
HRESULT APIENTRY GaDdiSetVertexShaderConstB(HANDLE hDevice, const D3DDDIARG_SETVERTEXSHADERCONSTB *pData,
                                            const BOOL *pRegisters);
HRESULT APIENTRY GaDdiSetScissorRect(HANDLE hDevice, const RECT *pRect);
HRESULT APIENTRY GaDdiSetStreamSource(HANDLE hDevice, const D3DDDIARG_SETSTREAMSOURCE *pData);
HRESULT APIENTRY GaDdiSetStreamSourceFreq(HANDLE hDevice, const D3DDDIARG_SETSTREAMSOURCEFREQ *pData);
HRESULT APIENTRY GaDdiSetConvolutionKernelMono(HANDLE hDevice, const D3DDDIARG_SETCONVOLUTIONKERNELMONO *pData);
HRESULT APIENTRY GaDdiComposeRects(HANDLE hDevice, const D3DDDIARG_COMPOSERECTS *pData);
HRESULT APIENTRY GaDdiBlt(HANDLE hDevice, const D3DDDIARG_BLT *pData);
HRESULT APIENTRY GaDdiColorFill(HANDLE hDevice, const D3DDDIARG_COLORFILL *pData);
HRESULT APIENTRY GaDdiDepthFill(HANDLE hDevice, const D3DDDIARG_DEPTHFILL *pData);
HRESULT APIENTRY GaDdiCreateQuery(HANDLE hDevice, D3DDDIARG_CREATEQUERY *pData);
HRESULT APIENTRY GaDdiDestroyQuery(HANDLE hDevice, HANDLE hQuery);
HRESULT APIENTRY GaDdiIssueQuery(HANDLE hDevice, const D3DDDIARG_ISSUEQUERY *pData);
HRESULT APIENTRY GaDdiGetQueryData(HANDLE hDevice, const D3DDDIARG_GETQUERYDATA *pData);
HRESULT APIENTRY GaDdiSetRenderTarget(HANDLE hDevice, const D3DDDIARG_SETRENDERTARGET *pData);
HRESULT APIENTRY GaDdiSetDepthStencil(HANDLE hDevice, const D3DDDIARG_SETDEPTHSTENCIL *pData);
HRESULT APIENTRY GaDdiGenerateMipSubLevels(HANDLE hDevice, const D3DDDIARG_GENERATEMIPSUBLEVELS *pData);
HRESULT APIENTRY GaDdiSetPixelShaderConstI(HANDLE hDevice, const D3DDDIARG_SETPIXELSHADERCONSTI *pData,
                                           const INT *pRegisters);
HRESULT APIENTRY GaDdiSetPixelShaderConstB(HANDLE hDevice, const D3DDDIARG_SETPIXELSHADERCONSTB *pData,
                                           const BOOL *pRegisters);
HRESULT APIENTRY GaDdiCreatePixelShader(HANDLE hDevice, D3DDDIARG_CREATEPIXELSHADER *pData, const UINT *pCode);
HRESULT APIENTRY GaDdiDeletePixelShader(HANDLE hDevice, HANDLE hShaderHandle);
HRESULT APIENTRY GaDdiCreateDecodeDevice(HANDLE hDevice, D3DDDIARG_CREATEDECODEDEVICE *pData);
HRESULT APIENTRY GaDdiDestroyDecodeDevice(HANDLE hDevice, HANDLE hDecodeDevice);
HRESULT APIENTRY GaDdiSetDecodeRenderTarget(HANDLE hDevice, const D3DDDIARG_SETDECODERENDERTARGET *pData);
HRESULT APIENTRY GaDdiDecodeBeginFrame(HANDLE hDevice, D3DDDIARG_DECODEBEGINFRAME* pData);
HRESULT APIENTRY GaDdiDecodeEndFrame(HANDLE hDevice, D3DDDIARG_DECODEENDFRAME* pData);
HRESULT APIENTRY GaDdiDecodeExecute(HANDLE hDevice, const D3DDDIARG_DECODEEXECUTE *pData);
HRESULT APIENTRY GaDdiDecodeExtensionExecute(HANDLE hDevice, const D3DDDIARG_DECODEEXTENSIONEXECUTE *pData);
HRESULT APIENTRY GaDdiCreateVideoProcessDevice(HANDLE hDevice, D3DDDIARG_CREATEVIDEOPROCESSDEVICE *pData);
HRESULT APIENTRY GaDdiDestroyVideoProcessDevice(HANDLE hDevice, HANDLE hVideoProcessor);
HRESULT APIENTRY GaDdiVideoProcessBeginFrame(HANDLE hDevice, HANDLE hVideoProcessor);
HRESULT APIENTRY GaDdiVideoProcessEndFrame(HANDLE hDevice, D3DDDIARG_VIDEOPROCESSENDFRAME* pData);
HRESULT APIENTRY GaDdiSetVideoProcessRenderTarget(HANDLE hDevice,
                                                  const D3DDDIARG_SETVIDEOPROCESSRENDERTARGET *pData);
HRESULT APIENTRY GaDdiVideoProcessBlt(HANDLE hDevice, const D3DDDIARG_VIDEOPROCESSBLT *pData);
HRESULT APIENTRY GaDdiCreateExtensionDevice(HANDLE hDevice, D3DDDIARG_CREATEEXTENSIONDEVICE *pData);
HRESULT APIENTRY GaDdiDestroyExtensionDevice(HANDLE hDevice, HANDLE hExtension);
HRESULT APIENTRY GaDdiExtensionExecute(HANDLE hDevice, const D3DDDIARG_EXTENSIONEXECUTE *pData);
HRESULT APIENTRY GaDdiCreateOverlay(HANDLE hDevice, D3DDDIARG_CREATEOVERLAY *pData);
HRESULT APIENTRY GaDdiUpdateOverlay(HANDLE hDevice, const D3DDDIARG_UPDATEOVERLAY *pData);
HRESULT APIENTRY GaDdiFlipOverlay(HANDLE hDevice, const D3DDDIARG_FLIPOVERLAY *pData);
HRESULT APIENTRY GaDdiGetOverlayColorControls(HANDLE hDevice, D3DDDIARG_GETOVERLAYCOLORCONTROLS *pData);
HRESULT APIENTRY GaDdiSetOverlayColorControls(HANDLE hDevice, const D3DDDIARG_SETOVERLAYCOLORCONTROLS *pData);
HRESULT APIENTRY GaDdiDestroyOverlay(HANDLE hDevice, const D3DDDIARG_DESTROYOVERLAY *pData);
HRESULT APIENTRY GaDdiDestroyDevice(HANDLE hDevice);
HRESULT APIENTRY GaDdiQueryResourceResidency(HANDLE hDevice, const D3DDDIARG_QUERYRESOURCERESIDENCY *pData);
HRESULT APIENTRY GaDdiOpenResource(HANDLE hDevice, D3DDDIARG_OPENRESOURCE *pResource);
HRESULT APIENTRY GaDdiGetCaptureAllocationHandle(HANDLE hDevice, D3DDDIARG_GETCAPTUREALLOCATIONHANDLE *pData);
HRESULT APIENTRY GaDdiCaptureToSysMem(HANDLE hDevice, const D3DDDIARG_CAPTURETOSYSMEM *pData);

HRESULT APIENTRY GaDdiAdapterGetCaps(HANDLE hAdapter, const D3DDDIARG_GETCAPS *pData);
HRESULT APIENTRY GaDdiAdapterCreateDevice(HANDLE hAdapter, D3DDDIARG_CREATEDEVICE *pCreateData);
HRESULT APIENTRY GaDdiAdapterCloseAdapter(IN HANDLE hAdapter);

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_GaDdi_h */
