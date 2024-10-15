/* $Id: VBoxGaD3DDevice9Ex.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - Gallium driver interface.
 *
 * GaDirect3DDevice9Ex implements IDirect3DDevice9Ex wrapper.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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

#include "VBoxGaD3DDevice9Ex.h"
#include "VBoxD3DAdapter9.h"
#include "GaDrvEnvWddm.h"

#include <iprt/asm.h>

/*
 * GaDirect3DDevice9Ex
 *
 * IDirect3DDevice9Ex wrapper for implementation in Gallium D3D9 state tracker "nine".
 */

GaDirect3DDevice9Ex::GaDirect3DDevice9Ex(IGaDirect3D9Ex *pD3D9Ex,
                                         HANDLE hAdapter,
                                         HANDLE hDevice,
                                         const D3DDDI_DEVICECALLBACKS *pDeviceCallbacks)
    :
    mcRefs(0),
    mhAdapter(hAdapter),
    mhDevice(hDevice),
    mpD3D9Ex(pD3D9Ex),
    mpStack(0),
    mpPresentationGroup(0),
    mpDevice(0)
{
    mpD3D9Ex->AddRef();
    mDeviceCallbacks = *pDeviceCallbacks;
}

GaDirect3DDevice9Ex::~GaDirect3DDevice9Ex()
{
    cleanup();
}

HRESULT GaDirect3DDevice9Ex::Init(D3DDEVTYPE DeviceType,
                                  HWND hFocusWindow,
                                  DWORD BehaviorFlags,
                                  D3DPRESENT_PARAMETERS* pPresentationParameters,
                                  D3DDISPLAYMODEEX* pFullscreenDisplayMode)
{
    mpStack = mpD3D9Ex->GetGalliumStack();
    mpStack->AddRef();

    HRESULT hr = WDDMPresentGroupCreate(this, &mpPresentationGroup);
    if (SUCCEEDED(hr))
    {
        /** @todo r=bird: The mpPresentationGroup parameter seems to always have been
         * consumed by the NineDevice9_ctor() code, while for the mpD3D9Ex parameter
         * it grabs a reference.  I've commented out the bogus looking
         * ID3DPresentGroup_Release call from NineDevice9_ctor() rather than balancing
         * reference to mpPresentionGroup here based on the hr value. See r153545.
         * Please verify and rework the fix to your liking.
         *
         * (The release call in cleanup() would call into no man's land early during
         * vlc.exe (v3.0.17.4) video startup on 32-bit w7 rtm.)
         */
        hr = D3DAdapter9_CreateDeviceEx(mpD3D9Ex->GetAdapter9(),
                                        D3DADAPTER_DEFAULT, DeviceType,
                                        hFocusWindow, BehaviorFlags,
                                        pPresentationParameters, pFullscreenDisplayMode,
                                        mpD3D9Ex, mpPresentationGroup,
                                        &mpDevice);
    }

    return hr;
}

void GaDirect3DDevice9Ex::cleanup()
{
    if (mpDevice)
    {
        mpDevice->Release();
        mpDevice = 0;
    }

    if (mpPresentationGroup)
    {
        mpPresentationGroup->Release();
        mpPresentationGroup = 0;
    }

    if (mpStack)
    {
        mpStack->Release();
        mpStack = 0;
    }

    if (mpD3D9Ex)
    {
        mpD3D9Ex->Release();
        mpD3D9Ex = 0;
    }
}

/* IUnknown wrappers. */
STDMETHODIMP_(ULONG) GaDirect3DDevice9Ex::AddRef()
{
    ULONG refs = InterlockedIncrement(&mcRefs);
    return refs;
}

STDMETHODIMP_(ULONG) GaDirect3DDevice9Ex::Release()
{
    ULONG refs = InterlockedDecrement(&mcRefs);
    if (refs == 0)
    {
        delete this;
    }

    return refs;
}

STDMETHODIMP GaDirect3DDevice9Ex::QueryInterface(REFIID riid,
                                                 void **ppvObject)
{
    if (!ppvObject)
    {
        return E_POINTER;
    }

    HRESULT hr = mpDevice?
                     mpDevice->QueryInterface(riid, ppvObject):
                     E_NOINTERFACE;
    if (FAILED(hr))
    {
        if (IsEqualGUID(IID_IGaDirect3DDevice9Ex, riid))
        {
            AddRef();
            *ppvObject = this;
            hr = S_OK;
        }
        else
        {
            *ppvObject = NULL;
            hr = E_NOINTERFACE;
        }
    }

    return hr;
}

#define GADEVICE9WRAP(name, params, vars) \
STDMETHODIMP GaDirect3DDevice9Ex::name params \
{ \
    if (mpDevice) \
        return mpDevice->name vars; \
    return E_FAIL; \
}

#define GADEVICE9WRAP_(type, name, params, vars) \
STDMETHODIMP_(type) GaDirect3DDevice9Ex::name params \
{ \
    if (mpDevice) \
        return mpDevice->name vars; \
    return 0; /** @todo retval */ \
}

#define GADEVICE9WRAPV(name, params, vars) \
STDMETHODIMP_(void) GaDirect3DDevice9Ex::name params \
{ \
    if (mpDevice) \
        return mpDevice->name vars; \
    return; \
}

GADEVICE9WRAP(TestCooperativeLevel,
                  (THIS),
                  ())
GADEVICE9WRAP_(UINT, GetAvailableTextureMem,
                  (THIS),
                  ())
GADEVICE9WRAP(EvictManagedResources,
                  (THIS),
                  ())
GADEVICE9WRAP(GetDirect3D,
                  (THIS_ IDirect3D9** ppD3D9),
                  (ppD3D9))
GADEVICE9WRAP(GetDeviceCaps,
                  (THIS_ D3DCAPS9* pCaps),
                  (pCaps))
GADEVICE9WRAP(GetDisplayMode,
                  (THIS_ UINT iSwapChain,D3DDISPLAYMODE* pMode),
                  (iSwapChain, pMode))
GADEVICE9WRAP(GetCreationParameters,
                  (THIS_ D3DDEVICE_CREATION_PARAMETERS *pParameters),
                  (pParameters))
GADEVICE9WRAP(SetCursorProperties,
                  (THIS_ UINT XHotSpot,UINT YHotSpot,IDirect3DSurface9* pCursorBitmap),
                  (XHotSpot, YHotSpot, pCursorBitmap))
GADEVICE9WRAPV(SetCursorPosition,
                  (THIS_ int X,int Y,DWORD Flags),
                  (X, Y, Flags))
GADEVICE9WRAP_(BOOL, ShowCursor,
                  (THIS_ BOOL bShow),
                  (bShow))
GADEVICE9WRAP(CreateAdditionalSwapChain,
                  (THIS_ D3DPRESENT_PARAMETERS* pPresentationParameters,IDirect3DSwapChain9** pSwapChain),
                  (pPresentationParameters, pSwapChain))
GADEVICE9WRAP(GetSwapChain,
                  (THIS_ UINT iSwapChain,IDirect3DSwapChain9** pSwapChain),
                  (iSwapChain, pSwapChain))
GADEVICE9WRAP_(UINT, GetNumberOfSwapChains,
                  (THIS),
                  ())
GADEVICE9WRAP(Reset,
                  (THIS_ D3DPRESENT_PARAMETERS* pPresentationParameters),
                  (pPresentationParameters))
GADEVICE9WRAP(Present,
                  (THIS_ CONST RECT* pSourceRect,CONST RECT* pDestRect,HWND hDestWindowOverride,CONST RGNDATA* pDirtyRegion),
                  (pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion))
GADEVICE9WRAP(GetBackBuffer,
                  (THIS_ UINT iSwapChain,UINT iBackBuffer,D3DBACKBUFFER_TYPE Type,IDirect3DSurface9** ppBackBuffer),
                  (iSwapChain, iBackBuffer, Type, ppBackBuffer))
GADEVICE9WRAP(GetRasterStatus,
                  (THIS_ UINT iSwapChain,D3DRASTER_STATUS* pRasterStatus),
                  (iSwapChain, pRasterStatus))
GADEVICE9WRAP(SetDialogBoxMode,
                  (THIS_ BOOL bEnableDialogs),
                  (bEnableDialogs))
GADEVICE9WRAPV(SetGammaRamp,
                  (THIS_ UINT iSwapChain,DWORD Flags,CONST D3DGAMMARAMP* pRamp),
                  (iSwapChain, Flags, pRamp))
GADEVICE9WRAPV(GetGammaRamp,
                  (THIS_ UINT iSwapChain,D3DGAMMARAMP* pRamp),
                  (iSwapChain, pRamp))
GADEVICE9WRAP(CreateTexture,
                  (THIS_ UINT Width,UINT Height,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DTexture9** ppTexture,HANDLE* pSharedHandle),
                  (Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle))
GADEVICE9WRAP(CreateVolumeTexture,
                  (THIS_ UINT Width,UINT Height,UINT Depth,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DVolumeTexture9** ppVolumeTexture,HANDLE* pSharedHandle),
                  (Width, Height, Depth, Levels, Usage, Format, Pool, ppVolumeTexture, pSharedHandle))
GADEVICE9WRAP(CreateCubeTexture,
                  (THIS_ UINT EdgeLength,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DCubeTexture9** ppCubeTexture,HANDLE* pSharedHandle),
                  (EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture, pSharedHandle))
GADEVICE9WRAP(CreateVertexBuffer,
                  (THIS_ UINT Length,DWORD Usage,DWORD FVF,D3DPOOL Pool,IDirect3DVertexBuffer9** ppVertexBuffer,HANDLE* pSharedHandle),
                  (Length, Usage, FVF, Pool, ppVertexBuffer, pSharedHandle))
GADEVICE9WRAP(CreateIndexBuffer,
                  (THIS_ UINT Length,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DIndexBuffer9** ppIndexBuffer,HANDLE* pSharedHandle),
                  (Length, Usage, Format, Pool, ppIndexBuffer, pSharedHandle))
GADEVICE9WRAP(CreateRenderTarget,
                  (THIS_ UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,DWORD MultisampleQuality,BOOL Lockable,IDirect3DSurface9** ppSurface,HANDLE* pSharedHandle),
                  (Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle))
GADEVICE9WRAP(CreateDepthStencilSurface,
                  (THIS_ UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,DWORD MultisampleQuality,BOOL Discard,IDirect3DSurface9** ppSurface,HANDLE* pSharedHandle),
                  (Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle))
GADEVICE9WRAP(UpdateSurface,
                  (THIS_ IDirect3DSurface9* pSourceSurface,CONST RECT* pSourceRect,IDirect3DSurface9* pDestinationSurface,CONST POINT* pDestPoint),
                  (pSourceSurface, pSourceRect, pDestinationSurface, pDestPoint))
GADEVICE9WRAP(UpdateTexture,
                  (THIS_ IDirect3DBaseTexture9* pSourceTexture,IDirect3DBaseTexture9* pDestinationTexture),
                  (pSourceTexture, pDestinationTexture))
GADEVICE9WRAP(GetRenderTargetData,
                  (THIS_ IDirect3DSurface9* pRenderTarget,IDirect3DSurface9* pDestSurface),
                  (pRenderTarget, pDestSurface))
GADEVICE9WRAP(GetFrontBufferData,
                  (THIS_ UINT iSwapChain,IDirect3DSurface9* pDestSurface),
                  (iSwapChain, pDestSurface))
GADEVICE9WRAP(StretchRect,
                  (THIS_ IDirect3DSurface9* pSourceSurface,CONST RECT* pSourceRect,IDirect3DSurface9* pDestSurface,CONST RECT* pDestRect,D3DTEXTUREFILTERTYPE Filter),
                  (pSourceSurface, pSourceRect, pDestSurface, pDestRect, Filter))
GADEVICE9WRAP(ColorFill,
                  (THIS_ IDirect3DSurface9* pSurface,CONST RECT* pRect,D3DCOLOR color),
                  (pSurface, pRect, color))
GADEVICE9WRAP(CreateOffscreenPlainSurface,
                  (THIS_ UINT Width,UINT Height,D3DFORMAT Format,D3DPOOL Pool,IDirect3DSurface9** ppSurface,HANDLE* pSharedHandle),
                  (Width, Height, Format, Pool, ppSurface, pSharedHandle))
GADEVICE9WRAP(SetRenderTarget,
                  (THIS_ DWORD RenderTargetIndex,IDirect3DSurface9* pRenderTarget),
                  (RenderTargetIndex, pRenderTarget))
GADEVICE9WRAP(GetRenderTarget,
                  (THIS_ DWORD RenderTargetIndex,IDirect3DSurface9** ppRenderTarget),
                  (RenderTargetIndex, ppRenderTarget))
GADEVICE9WRAP(SetDepthStencilSurface,
                  (THIS_ IDirect3DSurface9* pNewZStencil),
                  (pNewZStencil))
GADEVICE9WRAP(GetDepthStencilSurface,
                  (THIS_ IDirect3DSurface9** ppZStencilSurface),
                  (ppZStencilSurface))
GADEVICE9WRAP(BeginScene,
                  (THIS),
                  ())
GADEVICE9WRAP(EndScene,
                  (THIS),
                  ())
GADEVICE9WRAP(Clear,
                  (THIS_ DWORD Count,CONST D3DRECT* pRects,DWORD Flags,D3DCOLOR Color,float Z,DWORD Stencil),
                  (Count, pRects, Flags, Color, Z, Stencil))
GADEVICE9WRAP(SetTransform,
                  (THIS_ D3DTRANSFORMSTATETYPE State,CONST D3DMATRIX* pMatrix),
                  (State, pMatrix))
GADEVICE9WRAP(GetTransform,
                  (THIS_ D3DTRANSFORMSTATETYPE State,D3DMATRIX* pMatrix),
                  (State, pMatrix))
GADEVICE9WRAP(MultiplyTransform,
                  (THIS_ D3DTRANSFORMSTATETYPE State,CONST D3DMATRIX* pMatrix),
                  (State, pMatrix))
GADEVICE9WRAP(SetViewport,
                  (THIS_ CONST D3DVIEWPORT9* pViewport),
                  (pViewport))
GADEVICE9WRAP(GetViewport,
                  (THIS_ D3DVIEWPORT9* pViewport),
                  (pViewport))
GADEVICE9WRAP(SetMaterial,
                  (THIS_ CONST D3DMATERIAL9* pMaterial),
                  (pMaterial))
GADEVICE9WRAP(GetMaterial,
                  (THIS_ D3DMATERIAL9* pMaterial),
                  (pMaterial))
GADEVICE9WRAP(SetLight,
                  (THIS_ DWORD Index,CONST D3DLIGHT9* pLight),
                  (Index, pLight))
GADEVICE9WRAP(GetLight,
                  (THIS_ DWORD Index,D3DLIGHT9* pLight),
                  (Index, pLight))
GADEVICE9WRAP(LightEnable,
                  (THIS_ DWORD Index,BOOL Enable),
                  (Index, Enable))
GADEVICE9WRAP(GetLightEnable,
                  (THIS_ DWORD Index,BOOL* pEnable),
                  (Index, pEnable))
GADEVICE9WRAP(SetClipPlane,
                  (THIS_ DWORD Index,CONST float* pPlane),
                  (Index, pPlane))
GADEVICE9WRAP(GetClipPlane,
                  (THIS_ DWORD Index,float* pPlane),
                  (Index, pPlane))
GADEVICE9WRAP(SetRenderState,
                  (THIS_ D3DRENDERSTATETYPE State,DWORD Value),
                  (State, Value))
GADEVICE9WRAP(GetRenderState,
                  (THIS_ D3DRENDERSTATETYPE State,DWORD* pValue),
                  (State, pValue))
GADEVICE9WRAP(CreateStateBlock,
                  (THIS_ D3DSTATEBLOCKTYPE Type,IDirect3DStateBlock9** ppSB),
                  (Type, ppSB))
GADEVICE9WRAP(BeginStateBlock,
                  (THIS),
                  ())
GADEVICE9WRAP(EndStateBlock,
                  (THIS_ IDirect3DStateBlock9** ppSB),
                  (ppSB))
GADEVICE9WRAP(SetClipStatus,
                  (THIS_ CONST D3DCLIPSTATUS9* pClipStatus),
                  (pClipStatus))
GADEVICE9WRAP(GetClipStatus,
                  (THIS_ D3DCLIPSTATUS9* pClipStatus),
                  (pClipStatus))
GADEVICE9WRAP(GetTexture,
                  (THIS_ DWORD Stage,IDirect3DBaseTexture9** ppTexture),
                  (Stage, ppTexture))
GADEVICE9WRAP(SetTexture,
                  (THIS_ DWORD Stage,IDirect3DBaseTexture9* pTexture),
                  (Stage, pTexture))
GADEVICE9WRAP(GetTextureStageState,
                  (THIS_ DWORD Stage,D3DTEXTURESTAGESTATETYPE Type,DWORD* pValue),
                  (Stage, Type, pValue))
GADEVICE9WRAP(SetTextureStageState,
                  (THIS_ DWORD Stage,D3DTEXTURESTAGESTATETYPE Type,DWORD Value),
                  (Stage, Type, Value))
GADEVICE9WRAP(GetSamplerState,
                  (THIS_ DWORD Sampler,D3DSAMPLERSTATETYPE Type,DWORD* pValue),
                  (Sampler, Type, pValue))
GADEVICE9WRAP(SetSamplerState,
                  (THIS_ DWORD Sampler,D3DSAMPLERSTATETYPE Type,DWORD Value),
                  (Sampler, Type, Value))
GADEVICE9WRAP(ValidateDevice,
                  (THIS_ DWORD* pNumPasses),
                  (pNumPasses))
GADEVICE9WRAP(SetPaletteEntries,
                  (THIS_ UINT PaletteNumber,CONST PALETTEENTRY* pEntries),
                  (PaletteNumber, pEntries))
GADEVICE9WRAP(GetPaletteEntries,
                  (THIS_ UINT PaletteNumber,PALETTEENTRY* pEntries),
                  (PaletteNumber, pEntries))
GADEVICE9WRAP(SetCurrentTexturePalette,
                  (THIS_ UINT PaletteNumber),
                  (PaletteNumber))
GADEVICE9WRAP(GetCurrentTexturePalette,
                  (THIS_ UINT *PaletteNumber),
                  (PaletteNumber))
GADEVICE9WRAP(SetScissorRect,
                  (THIS_ CONST RECT* pRect),
                  (pRect))
GADEVICE9WRAP(GetScissorRect,
                  (THIS_ RECT* pRect),
                  (pRect))
GADEVICE9WRAP(SetSoftwareVertexProcessing,
                  (THIS_ BOOL bSoftware),
                  (bSoftware))
GADEVICE9WRAP_(BOOL, GetSoftwareVertexProcessing,
                  (THIS),
                  ())
GADEVICE9WRAP(SetNPatchMode,
                  (THIS_ float nSegments),
                  (nSegments))
GADEVICE9WRAP_(float, GetNPatchMode,
                  (THIS),
                  ())
GADEVICE9WRAP(DrawPrimitive,
                  (THIS_ D3DPRIMITIVETYPE PrimitiveType,UINT StartVertex,UINT PrimitiveCount),
                  (PrimitiveType, StartVertex, PrimitiveCount))
GADEVICE9WRAP(DrawIndexedPrimitive,
                  (THIS_ D3DPRIMITIVETYPE Primitive,INT BaseVertexIndex,UINT MinVertexIndex,UINT NumVertices,UINT startIndex,UINT primCount),
                  (Primitive, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount))
GADEVICE9WRAP(DrawPrimitiveUP,
                  (THIS_ D3DPRIMITIVETYPE PrimitiveType,UINT PrimitiveCount,CONST void* pVertexStreamZeroData,UINT VertexStreamZeroStride),
                  (PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride))
GADEVICE9WRAP(DrawIndexedPrimitiveUP,
                  (THIS_ D3DPRIMITIVETYPE PrimitiveType,UINT MinVertexIndex,UINT NumVertices,UINT PrimitiveCount,CONST void* pIndexData,D3DFORMAT IndexDataFormat,CONST void* pVertexStreamZeroData,UINT VertexStreamZeroStride),
                  (PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride))
GADEVICE9WRAP(ProcessVertices,
                  (THIS_ UINT SrcStartIndex,UINT DestIndex,UINT VertexCount,IDirect3DVertexBuffer9* pDestBuffer,IDirect3DVertexDeclaration9* pVertexDecl,DWORD Flags),
                  (SrcStartIndex, DestIndex, VertexCount, pDestBuffer, pVertexDecl, Flags))
GADEVICE9WRAP(CreateVertexDeclaration,
                  (THIS_ CONST D3DVERTEXELEMENT9* pVertexElements,IDirect3DVertexDeclaration9** ppDecl),
                  (pVertexElements, ppDecl))
GADEVICE9WRAP(SetVertexDeclaration,
                  (THIS_ IDirect3DVertexDeclaration9* pDecl),
                  (pDecl))
GADEVICE9WRAP(GetVertexDeclaration,
                  (THIS_ IDirect3DVertexDeclaration9** ppDecl),
                  (ppDecl))
GADEVICE9WRAP(SetFVF,
                  (THIS_ DWORD FVF),
                  (FVF))
GADEVICE9WRAP(GetFVF,
                  (THIS_ DWORD* pFVF),
                  (pFVF))
GADEVICE9WRAP(CreateVertexShader,
                  (THIS_ CONST DWORD* pFunction,IDirect3DVertexShader9** ppShader),
                  (pFunction, ppShader))
GADEVICE9WRAP(SetVertexShader,
                  (THIS_ IDirect3DVertexShader9* pShader),
                  (pShader))
GADEVICE9WRAP(GetVertexShader,
                  (THIS_ IDirect3DVertexShader9** ppShader),
                  (ppShader))
GADEVICE9WRAP(SetVertexShaderConstantF,
                  (THIS_ UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount),
                  (StartRegister, pConstantData, Vector4fCount))
GADEVICE9WRAP(GetVertexShaderConstantF,
                  (THIS_ UINT StartRegister,float* pConstantData,UINT Vector4fCount),
                  (StartRegister, pConstantData, Vector4fCount))
GADEVICE9WRAP(SetVertexShaderConstantI,
                  (THIS_ UINT StartRegister,CONST int* pConstantData,UINT Vector4iCount),
                  (StartRegister, pConstantData, Vector4iCount))
GADEVICE9WRAP(GetVertexShaderConstantI,
                  (THIS_ UINT StartRegister,int* pConstantData,UINT Vector4iCount),
                  (StartRegister, pConstantData, Vector4iCount))
GADEVICE9WRAP(SetVertexShaderConstantB,
                  (THIS_ UINT StartRegister,CONST BOOL* pConstantData,UINT  BoolCount),
                  (StartRegister, pConstantData, BoolCount))
GADEVICE9WRAP(GetVertexShaderConstantB,
                  (THIS_ UINT StartRegister,BOOL* pConstantData,UINT BoolCount),
                  (StartRegister, pConstantData, BoolCount))
GADEVICE9WRAP(SetStreamSource,
                  (THIS_ UINT StreamNumber,IDirect3DVertexBuffer9* pStreamData,UINT OffsetInBytes,UINT Stride),
                  (StreamNumber, pStreamData, OffsetInBytes, Stride))
GADEVICE9WRAP(GetStreamSource,
                  (THIS_ UINT StreamNumber,IDirect3DVertexBuffer9** ppStreamData,UINT* pOffsetInBytes,UINT* pStride),
                  (StreamNumber, ppStreamData, pOffsetInBytes, pStride))
GADEVICE9WRAP(SetStreamSourceFreq,
                  (THIS_ UINT StreamNumber,UINT Setting),
                  (StreamNumber, Setting))
GADEVICE9WRAP(GetStreamSourceFreq,
                  (THIS_ UINT StreamNumber,UINT* pSetting),
                  (StreamNumber, pSetting))
GADEVICE9WRAP(SetIndices,
                  (THIS_ IDirect3DIndexBuffer9* pIndexData),
                  (pIndexData))
GADEVICE9WRAP(GetIndices,
                  (THIS_ IDirect3DIndexBuffer9** ppIndexData),
                  (ppIndexData))
GADEVICE9WRAP(CreatePixelShader,
                  (THIS_ CONST DWORD* pFunction,IDirect3DPixelShader9** ppShader),
                  (pFunction, ppShader))
GADEVICE9WRAP(SetPixelShader,
                  (THIS_ IDirect3DPixelShader9* pShader),
                  (pShader))
GADEVICE9WRAP(GetPixelShader,
                  (THIS_ IDirect3DPixelShader9** ppShader),
                  (ppShader))
GADEVICE9WRAP(SetPixelShaderConstantF,
                  (THIS_ UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount),
                  (StartRegister, pConstantData, Vector4fCount))
GADEVICE9WRAP(GetPixelShaderConstantF,
                  (THIS_ UINT StartRegister,float* pConstantData,UINT Vector4fCount),
                  (StartRegister, pConstantData, Vector4fCount))
GADEVICE9WRAP(SetPixelShaderConstantI,
                  (THIS_ UINT StartRegister,CONST int* pConstantData,UINT Vector4iCount),
                  (StartRegister, pConstantData, Vector4iCount))
GADEVICE9WRAP(GetPixelShaderConstantI,
                  (THIS_ UINT StartRegister,int* pConstantData,UINT Vector4iCount),
                  (StartRegister, pConstantData, Vector4iCount))
GADEVICE9WRAP(SetPixelShaderConstantB,
                  (THIS_ UINT StartRegister,CONST BOOL* pConstantData,UINT  BoolCount),
                  (StartRegister, pConstantData, BoolCount))
GADEVICE9WRAP(GetPixelShaderConstantB,
                  (THIS_ UINT StartRegister,BOOL* pConstantData,UINT BoolCount),
                  (StartRegister, pConstantData, BoolCount))
GADEVICE9WRAP(DrawRectPatch,
                  (THIS_ UINT Handle,CONST float* pNumSegs,CONST D3DRECTPATCH_INFO* pRectPatchInfo),
                  (Handle, pNumSegs, pRectPatchInfo))
GADEVICE9WRAP(DrawTriPatch,
                  (THIS_ UINT Handle,CONST float* pNumSegs,CONST D3DTRIPATCH_INFO* pTriPatchInfo),
                  (Handle, pNumSegs, pTriPatchInfo))
GADEVICE9WRAP(DeletePatch,
                  (THIS_ UINT Handle),
                  (Handle))
GADEVICE9WRAP(CreateQuery,
                  (THIS_ D3DQUERYTYPE Type,IDirect3DQuery9** ppQuery),
                  (Type, ppQuery))
GADEVICE9WRAP(SetConvolutionMonoKernel,
                  (THIS_ UINT width,UINT height,float* rows,float* columns),
                  (width, height, rows, columns))
GADEVICE9WRAP(ComposeRects,
                  (THIS_ IDirect3DSurface9* pSrc,IDirect3DSurface9* pDst,IDirect3DVertexBuffer9* pSrcRectDescs,UINT NumRects,IDirect3DVertexBuffer9* pDstRectDescs,D3DCOMPOSERECTSOP Operation,int Xoffset,int Yoffset),
                  (pSrc, pDst, pSrcRectDescs, NumRects, pDstRectDescs, Operation, Xoffset, Yoffset))
GADEVICE9WRAP(PresentEx,
                  (THIS_ CONST RECT* pSourceRect,CONST RECT* pDestRect,HWND hDestWindowOverride,CONST RGNDATA* pDirtyRegion,DWORD dwFlags),
                  (pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags))
GADEVICE9WRAP(GetGPUThreadPriority,
                  (THIS_ INT* pPriority),
                  (pPriority))
GADEVICE9WRAP(SetGPUThreadPriority,
                  (THIS_ INT Priority),
                  (Priority))
GADEVICE9WRAP(WaitForVBlank,
                  (THIS_ UINT iSwapChain),
                  (iSwapChain))
GADEVICE9WRAP(CheckResourceResidency,
                  (THIS_ IDirect3DResource9** pResourceArray,UINT32 NumResources),
                  (pResourceArray, NumResources))
GADEVICE9WRAP(SetMaximumFrameLatency,
                  (THIS_ UINT MaxLatency),
                  (MaxLatency))
GADEVICE9WRAP(GetMaximumFrameLatency,
                  (THIS_ UINT* pMaxLatency),
                  (pMaxLatency))
GADEVICE9WRAP(CheckDeviceState,
                  (THIS_ HWND hDestinationWindow),
                  (hDestinationWindow))
GADEVICE9WRAP(CreateRenderTargetEx,
                  (THIS_ UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,DWORD MultisampleQuality,BOOL Lockable,IDirect3DSurface9** ppSurface,HANDLE* pSharedHandle,DWORD Usage),
                  (Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle, Usage))
GADEVICE9WRAP(CreateOffscreenPlainSurfaceEx,
                  (THIS_ UINT Width,UINT Height,D3DFORMAT Format,D3DPOOL Pool,IDirect3DSurface9** ppSurface,HANDLE* pSharedHandle,DWORD Usage),
                  (Width, Height, Format, Pool, ppSurface, pSharedHandle, Usage))
GADEVICE9WRAP(CreateDepthStencilSurfaceEx,
                  (THIS_ UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,DWORD MultisampleQuality,BOOL Discard,IDirect3DSurface9** ppSurface,HANDLE* pSharedHandle,DWORD Usage),
                  (Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle, Usage))
GADEVICE9WRAP(ResetEx,
                  (THIS_ D3DPRESENT_PARAMETERS* pPresentationParameters,D3DDISPLAYMODEEX *pFullscreenDisplayMode),
                  (pPresentationParameters, pFullscreenDisplayMode))
GADEVICE9WRAP(GetDisplayModeEx,
                  (THIS_ UINT iSwapChain,D3DDISPLAYMODEEX* pMode,D3DDISPLAYROTATION* pRotation),
                  (iSwapChain, pMode, pRotation))

#undef GADEVICE9WRAPV
#undef GADEVICE9WRAP_
#undef GADEVICE9WRAP


/*
 * IGaDirect3DDevice9Ex methods.
 */
STDMETHODIMP GaDirect3DDevice9Ex::GaSurfaceId(IUnknown *pSurface, uint32_t *pu32Sid)
{
#if VBOX_MESA_V_MAJOR < 24
    struct pipe_resource *pResource = mpStack->GaNinePipeResourceFromSurface(pSurface);
    if (pResource)
    {
        struct pipe_screen *pScreen = mpD3D9Ex->GetScreen();
        *pu32Sid = mpStack->GaDrvGetSurfaceId(pScreen, pResource);
    }
#else
    *pu32Sid = mpStack->GaNineGetSurfaceId(pSurface);
#endif

    return S_OK;
}

STDMETHODIMP GaDirect3DDevice9Ex::GaWDDMContextHandle(HANDLE *phContext)
{
#if VBOX_MESA_V_MAJOR < 24
    struct pipe_context *pPipeContext = mpStack->GaNinePipeContextFromDevice(this->mpDevice);
    if (pPipeContext)
    {
        struct pipe_screen *pScreen = mpD3D9Ex->GetScreen();
        WDDMGalliumDriverEnv const *pEnv = mpStack->GaDrvGetWDDMEnv(pScreen);
        if (pEnv)
        {
            uint32_t u32Cid = mpStack->GaDrvGetContextId(pPipeContext);

            GaDrvEnvWddm *pEnvWddm = (GaDrvEnvWddm *)pEnv->pvEnv;
            *phContext = pEnvWddm->GaDrvEnvWddmContextHandle(u32Cid);
        }
    }
#else
    uint32_t u32Cid = mpStack->GaNineGetContextId(mpDevice);
    WDDMGalliumDriverEnv const *pEnv = mpD3D9Ex->GetWDDMEnv();
    if (pEnv)
    {
        GaDrvEnvWddm *pEnvWddm = (GaDrvEnvWddm *)pEnv->pvEnv;
        *phContext = pEnvWddm->GaDrvEnvWddmContextHandle(u32Cid);
    }
#endif

    return S_OK;
}

STDMETHODIMP GaDirect3DDevice9Ex::GaFlush()
{
#if VBOX_MESA_V_MAJOR < 24
    struct pipe_context *pPipeContext = mpStack->GaNinePipeContextFromDevice(this->mpDevice);
    if (pPipeContext)
    {
        mpStack->GaDrvContextFlush(pPipeContext);
    }
#else
    mpStack->GaNineFlush(this->mpDevice);
#endif

    return S_OK;
}

STDMETHODIMP GaDirect3DDevice9Ex::EscapeCb(const void *pvData, uint32_t cbData, bool fHardwareAccess)
{
    HANDLE hContext = 0;
    HRESULT hr = GaWDDMContextHandle(&hContext);
    if (SUCCEEDED(hr))
    {
        D3DDDICB_ESCAPE ddiEscape;
        ddiEscape.hDevice               = mhDevice;
        ddiEscape.Flags.Value           = 0;
        if (fHardwareAccess)
        {
            ddiEscape.Flags.HardwareAccess = 1;
        }
        ddiEscape.pPrivateDriverData    = (void *)pvData;
        ddiEscape.PrivateDriverDataSize = cbData;
        ddiEscape.hContext              = hContext;

        hr = mDeviceCallbacks.pfnEscapeCb(mhAdapter, &ddiEscape);
    }

    return hr;
}
