/* $Id: DevVGA-SVGA3d-dx-dx11.cpp 110335 2025-07-21 10:05:19Z vitali.pelenjow@oracle.com $ */
/** @file
 * DevVMWare - VMWare SVGA device
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_VMSVGA
#include <VBox/AssertGuest.h>
#include <VBox/log.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pgm.h>

#include <iprt/asm-mem.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>

#include <VBoxVideo.h> /* required by DevVGA.h */
#include <VBoxVideo3D.h>

/* should go BEFORE any other DevVGA include to make all DevVGA.h config defines be visible */
#include "DevVGA.h"

#include "DevVGA-SVGA.h"
#include "DevVGA-SVGA3d.h"
#include "DevVGA-SVGA3d-internal.h"
#include "DevVGA-SVGA3d-dx-shader.h"

/* d3d11_1.h has a structure field named 'Status' but Status is defined as int on Linux host */
#if defined(Status)
#undef Status
#endif
#ifndef RT_OS_WINDOWS
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include <d3d11_1.h>
#ifndef RT_OS_WINDOWS
# pragma GCC diagnostic pop
#endif


/* One ID3D11Device object is used for all VMSVGA guest contexts because the VGPU design makes resources
 * independent from rendering contexts. I.e. multiple guest contexts freely access a surface.
 *
 * The initial implementation of this backend has used separate ID3D11Devices for each VMSVGA context
 * and created shared resources to allow one ID3D11Device to access a resource which was rendered to by
 * another ID3D11Device. This synchronization of access to shared resources kills performance actually.
 */

/* Always flush after submitting a draw call for debugging. */
//#define DX_FLUSH_AFTER_DRAW

#define D3D_RELEASE_ARRAY(a_Count, a_papArray) do { \
    for (uint32_t i = 0; i < (a_Count); ++i) \
        D3D_RELEASE((a_papArray)[i]); \
} while (0)

typedef struct D3D11BLITTER
{
    ID3D11Device1          *pDevice;
    ID3D11DeviceContext1   *pImmediateContext;

    ID3D11VertexShader     *pVertexShader;
    ID3D11PixelShader      *pPixelShader;
    ID3D11SamplerState     *pSamplerState;
    ID3D11RasterizerState1 *pRasterizerState;
    ID3D11BlendState1      *pBlendState;
} D3D11BLITTER;

typedef struct DXDEVICE
{
    ID3D11Device1              *pDevice;               /* Device. */
    ID3D11DeviceContext1       *pImmediateContext;     /* Corresponding context. */
    IDXGIFactory               *pDxgiFactory;          /* DXGI Factory. */
    D3D_FEATURE_LEVEL           FeatureLevel;

    uint32_t                    MultisampleCountMask;  /* 1 << (MSCount - 1) for MSCount = 2, 4, 8, 16, 32 */

    ID3D11VideoDevice          *pVideoDevice;
    ID3D11VideoContext         *pVideoContext;

    D3D11BLITTER               Blitter;                /* Blits one texture to another. */
} DXDEVICE;

/* Kind of a texture view. */
typedef enum VMSVGA3DBACKVIEWTYPE
{
    VMSVGA3D_VIEWTYPE_NONE           = 0,
    VMSVGA3D_VIEWTYPE_RENDERTARGET   = 1,
    VMSVGA3D_VIEWTYPE_DEPTHSTENCIL   = 2,
    VMSVGA3D_VIEWTYPE_SHADERRESOURCE = 3,
    VMSVGA3D_VIEWTYPE_UNORDEREDACCESS = 4,
    VMSVGA3D_VIEWTYPE_VIDEODECODEROUTPUT = 5,
    VMSVGA3D_VIEWTYPE_VIDEOPROCESSORINPUT = 6,
    VMSVGA3D_VIEWTYPE_VIDEOPROCESSOROUTPUT = 7
} VMSVGA3DBACKVIEWTYPE;

/* Information about a texture view to track all created views:.
 * when a surface is invalidated, then all views must deleted;
 * when a view is deleted, then the view must be unlinked from the surface.
 */
typedef struct DXVIEWINFO
{
    uint32_t sid;                                      /* Surface which the view was created for. */
    uint32_t cid;                                      /* DX context which created the view. */
    uint32_t viewId;                                   /* View id assigned by the guest. */
    VMSVGA3DBACKVIEWTYPE enmViewType;
} DXVIEWINFO;

/* Context Object Table element for a texture view. */
typedef struct DXVIEW
{
    uint32_t cid;                                      /* DX context which created the view. */
    uint32_t sid;                                      /* Surface which the view was created for. */
    uint32_t viewId;                                   /* View id assigned by the guest. */
    VMSVGA3DBACKVIEWTYPE enmViewType;

    union
    {
        ID3D11View               *pView;               /* The view object. */
        ID3D11RenderTargetView   *pRenderTargetView;
        ID3D11DepthStencilView   *pDepthStencilView;
        ID3D11ShaderResourceView *pShaderResourceView;
        ID3D11UnorderedAccessView *pUnorderedAccessView;
        ID3D11VideoDecoderOutputView *pVideoDecoderOutputView;
        ID3D11VideoProcessorInputView *pVideoProcessorInputView;
        ID3D11VideoProcessorOutputView *pVideoProcessorOutputView;
    } u;

    RTLISTNODE nodeSurfaceView;                        /* Views are linked to the surface. */
} DXVIEW;

/* What kind of resource has been created for the VMSVGA3D surface. */
typedef enum VMSVGA3DBACKRESTYPE
{
    VMSVGA3D_RESTYPE_NONE           = 0,
    VMSVGA3D_RESTYPE_TEXTURE_1D     = 1,
    VMSVGA3D_RESTYPE_TEXTURE_2D     = 2,
    VMSVGA3D_RESTYPE_TEXTURE_CUBE   = 3,
    VMSVGA3D_RESTYPE_TEXTURE_3D     = 4,
    VMSVGA3D_RESTYPE_BUFFER         = 5,
} VMSVGA3DBACKRESTYPE;

typedef struct VMSVGA3DBACKENDSURFACE
{
    VMSVGA3DBACKRESTYPE enmResType;
    DXGI_FORMAT enmDxgiFormat;
    D3D11_USAGE enmD3D11Usage;
    union
    {
        ID3D11Resource     *pResource;
        ID3D11Texture1D    *pTexture1D;
        ID3D11Texture2D    *pTexture2D;
        ID3D11Texture3D    *pTexture3D;
        ID3D11Buffer       *pBuffer;
    } u;

    /* For updates from memory. */
    union /** @todo One per format. */
    {
        ID3D11Resource     *pResource;
        ID3D11Texture1D    *pTexture1D;
        ID3D11Texture2D    *pTexture2D;
        ID3D11Texture3D    *pTexture3D;
        ID3D11Buffer       *pBuffer;
    } dynamic;

    /* For reading the texture content. */
    union /** @todo One per format. */
    {
        ID3D11Resource     *pResource;
        ID3D11Texture1D    *pTexture1D;
        ID3D11Texture2D    *pTexture2D;
        ID3D11Texture3D    *pTexture3D;
        ID3D11Buffer       *pBuffer;
    } staging;

    /* Render target views, depth stencil views and shader resource views created for this texture or buffer. */
    RTLISTANCHOR listView;                        /* DXVIEW */

} VMSVGA3DBACKENDSURFACE;


typedef struct VMSVGAHWSCREEN
{
    ID3D11Texture2D            *pTexture;         /* Shared texture for the screen content. Only used as CopyResource target. */
    IDXGIResource              *pDxgiResource;    /* Interface of the texture. */
    IDXGIKeyedMutex            *pDXGIKeyedMutex;  /* Synchronization interface for the render device. */
    HANDLE                      SharedHandle;     /* The shared handle of this structure. */
    uint32_t                    sidScreenTarget;  /* The source surface for this screen. */
} VMSVGAHWSCREEN;


typedef struct DXELEMENTLAYOUT
{
    ID3D11InputLayout          *pElementLayout;
    uint32_t                    cElementDesc;
    D3D11_INPUT_ELEMENT_DESC    aElementDesc[32];
} DXELEMENTLAYOUT;

typedef struct DXSHADER
{
    SVGA3dShaderType            enmShaderType;
    union
    {
        ID3D11DeviceChild      *pShader;            /* All. */
        ID3D11VertexShader     *pVertexShader;      /* SVGA3D_SHADERTYPE_VS */
        ID3D11PixelShader      *pPixelShader;       /* SVGA3D_SHADERTYPE_PS */
        ID3D11GeometryShader   *pGeometryShader;    /* SVGA3D_SHADERTYPE_GS */
        ID3D11HullShader       *pHullShader;        /* SVGA3D_SHADERTYPE_HS */
        ID3D11DomainShader     *pDomainShader;      /* SVGA3D_SHADERTYPE_DS */
        ID3D11ComputeShader    *pComputeShader;     /* SVGA3D_SHADERTYPE_CS */
    };
    void                       *pvDXBC;
    uint32_t                    cbDXBC;

    uint32_t                    soid;               /* Stream output declarations for geometry shaders. */

    DXShaderInfo                shaderInfo;
} DXSHADER;

typedef struct DXQUERY
{
    union
    {
        ID3D11Query            *pQuery;
        ID3D11Predicate        *pPredicate;
    };
} DXQUERY;

typedef struct DXVIDEOPROCESSOR
{
    ID3D11VideoProcessorEnumerator *pEnum;
    ID3D11VideoProcessor       *pVideoProcessor;
} DXVIDEOPROCESSOR;

typedef struct DXVIDEODECODER
{
    ID3D11VideoDecoder         *pVideoDecoder;
} DXVIDEODECODER;

typedef struct DXSTREAMOUTPUT
{
    UINT                       cDeclarationEntry;
    D3D11_SO_DECLARATION_ENTRY aDeclarationEntry[SVGA3D_MAX_STREAMOUT_DECLS];
} DXSTREAMOUTPUT;

typedef struct DXBOUNDVERTEXBUFFER
{
    ID3D11Buffer *pBuffer;
    uint32_t stride;
    uint32_t offset;
} DXBOUNDVERTEXBUFFER;

typedef struct DXBOUNDINDEXBUFFER
{
    ID3D11Buffer *pBuffer;
    DXGI_FORMAT indexBufferFormat;
    uint32_t indexBufferOffset;
} DXBOUNDINDEXBUFFER;

typedef struct DXBOUNDRESOURCES /* Currently bound resources. Mirror SVGADXContextMobFormat structure. */
{
    struct
    {
        ID3D11Buffer *constantBuffers[SVGA3D_DX_MAX_CONSTBUFFERS];
    } shaderState[SVGA3D_NUM_SHADERTYPE];
} DXBOUNDRESOURCES;


typedef struct VMSVGA3DBACKENDDXCONTEXT
{
    /* Arrays for Context-Object Tables. Number of entries depends on COTable size. */
    uint32_t                   cBlendState;            /* Number of entries in the papBlendState array. */
    uint32_t                   cDepthStencilState;     /* papDepthStencilState */
    uint32_t                   cSamplerState;          /* papSamplerState */
    uint32_t                   cRasterizerState;       /* papRasterizerState */
    uint32_t                   cElementLayout;         /* paElementLayout */
    uint32_t                   cRenderTargetView;      /* paRenderTargetView */
    uint32_t                   cDepthStencilView;      /* paDepthStencilView */
    uint32_t                   cShaderResourceView;    /* paShaderResourceView */
    uint32_t                   cQuery;                 /* paQuery */
    uint32_t                   cShader;                /* paShader */
    uint32_t                   cStreamOutput;          /* paStreamOutput */
    uint32_t                   cUnorderedAccessView;   /* paUnorderedAccessView */
    ID3D11BlendState1        **papBlendState;
    ID3D11DepthStencilState  **papDepthStencilState;
    ID3D11SamplerState       **papSamplerState;
    ID3D11RasterizerState1   **papRasterizerState;
    DXELEMENTLAYOUT           *paElementLayout;
    DXVIEW                    *paRenderTargetView;
    DXVIEW                    *paDepthStencilView;
    DXVIEW                    *paShaderResourceView;
    DXQUERY                   *paQuery;
    DXSHADER                  *paShader;
    DXSTREAMOUTPUT            *paStreamOutput;
    DXVIEW                    *paUnorderedAccessView;

    uint32_t                   cVideoProcessor;        /* paVideoProcessor */
    uint32_t                   cVideoDecoderOutputView; /* paVideoDecoderOutputView */
    uint32_t                   cVideoDecoder;          /* paVideoDecoder */
    uint32_t                   cVideoProcessorInputView; /* paVideoProcessorInputView */
    uint32_t                   cVideoProcessorOutputView; /* paVideoProcessorOutputView */
    DXVIDEOPROCESSOR          *paVideoProcessor;
    DXVIEW                    *paVideoDecoderOutputView;
    DXVIDEODECODER            *paVideoDecoder;
    DXVIEW                    *paVideoProcessorInputView;
    DXVIEW                    *paVideoProcessorOutputView;

    uint32_t                   cSOTarget;              /* How many SO targets are currently set (SetSOTargets) */

    DXBOUNDRESOURCES           resources;
} VMSVGA3DBACKENDDXCONTEXT;

/* Shader disassembler function. Optional. */
typedef HRESULT FN_D3D_DISASSEMBLE(LPCVOID pSrcData, SIZE_T SrcDataSize, UINT Flags, LPCSTR szComments, ID3D10Blob **ppDisassembly);
typedef FN_D3D_DISASSEMBLE *PFN_D3D_DISASSEMBLE;

typedef struct VMSVGA3DBACKEND
{
    RTLDRMOD                   hD3D11;
    PFN_D3D11_CREATE_DEVICE    pfnD3D11CreateDevice;

    RTLDRMOD                   hD3DCompiler;
    PFN_D3D_DISASSEMBLE        pfnD3DDisassemble;

    DXDEVICE                   dxDevice;
    UINT                       VendorId;
    UINT                       DeviceId;

    SVGADXContextMobFormat     svgaDXContext;          /* Current state of pipeline. */

    DXBOUNDRESOURCES           resources;              /* What is currently applied to the pipeline. */
} VMSVGA3DBACKEND;


/* Static function prototypes. */
static int dxDeviceFlush(DXDEVICE *pDevice);
static int dxSetRenderTargets(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext);
static int dxSetCSUnorderedAccessViews(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext);
static DECLCALLBACK(void) vmsvga3dBackSurfaceDestroy(PVGASTATECC pThisCC, bool fClearCOTableEntry, PVMSVGA3DSURFACE pSurface);
static int dxDestroyShader(DXSHADER *pDXShader);
static int dxDestroyQuery(DXQUERY *pDXQuery);
static int dxReadBuffer(DXDEVICE *pDevice, ID3D11Buffer *pBuffer, UINT Offset, UINT Bytes, void **ppvData, uint32_t *pcbData);

static int dxCreateVideoProcessor(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId, VBSVGACOTableDXVideoProcessorEntry const *pEntry);
static void dxDestroyVideoProcessor(DXVIDEOPROCESSOR *pDXVideoProcessor);
static int dxCreateVideoDecoder(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoDecoderId videoDecoderId, VBSVGACOTableDXVideoDecoderEntry const *pEntry);
static void dxDestroyVideoDecoder(DXVIDEODECODER *pDXVideoDecoder);

static HRESULT BlitInit(D3D11BLITTER *pBlitter, ID3D11Device1 *pDevice, ID3D11DeviceContext1 *pImmediateContext);
static void BlitRelease(D3D11BLITTER *pBlitter);


#if defined(VBOX_WITH_DXVK)
# define VBOX_DX_LIBRARY "VBoxDxVk"
#elif defined(VBOX_WITH_DXMT)
# define VBOX_DX_LIBRARY "VBoxDxMt"
#endif

static int dxLoadD3D11Library(RTLDRMOD *phD3D11)
{
#if defined(VBOX_DX_LIBRARY)
    RTERRINFOSTATIC ErrInfo;
    int rc = SUPR3HardenedLdrLoadAppPriv(VBOX_DX_LIBRARY, phD3D11, RTLDRLOAD_FLAGS_LOCAL, RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(rc))
    {
        LogRel(("VMSVGA: Failed to load %s: %Rrc\n", VBOX_DX_LIBRARY, rc));
        if (RTErrInfoIsSet(&ErrInfo.Core))
            LogRel(("VMSVGA: %#RTeic\n", &ErrInfo.Core));
    }
    return rc;
#elif defined(RT_OS_WINDOWS)
    return RTLdrLoadSystem("d3d11", /* fNoUnload = */ true, phD3D11);
#else
# error "No D3D11 library"
#endif
}


/* This is not available with the DXVK headers for some reason. */
#ifndef RT_OS_WINDOWS
typedef enum D3D11_TEXTURECUBE_FACE {
  D3D11_TEXTURECUBE_FACE_POSITIVE_X,
  D3D11_TEXTURECUBE_FACE_NEGATIVE_X,
  D3D11_TEXTURECUBE_FACE_POSITIVE_Y,
  D3D11_TEXTURECUBE_FACE_NEGATIVE_Y,
  D3D11_TEXTURECUBE_FACE_POSITIVE_Z,
  D3D11_TEXTURECUBE_FACE_NEGATIVE_Z
} D3D11_TEXTURECUBE_FACE;
#endif


#if 0 /* unused */
DECLINLINE(D3D11_TEXTURECUBE_FACE) vmsvga3dCubemapFaceFromIndex(uint32_t iFace)
{
    D3D11_TEXTURECUBE_FACE Face;
    switch (iFace)
    {
        case 0: Face = D3D11_TEXTURECUBE_FACE_POSITIVE_X; break;
        case 1: Face = D3D11_TEXTURECUBE_FACE_NEGATIVE_X; break;
        case 2: Face = D3D11_TEXTURECUBE_FACE_POSITIVE_Y; break;
        case 3: Face = D3D11_TEXTURECUBE_FACE_NEGATIVE_Y; break;
        case 4: Face = D3D11_TEXTURECUBE_FACE_POSITIVE_Z; break;
        default:
        case 5: Face = D3D11_TEXTURECUBE_FACE_NEGATIVE_Z; break;
    }
    return Face;
}
#endif

#ifdef LOG_ENABLED
static const char *dxFormatName(DXGI_FORMAT dxgiFormat)
{
    switch (dxgiFormat)
    {
        RT_CASE_RET_STR(DXGI_FORMAT_UNKNOWN);
        RT_CASE_RET_STR(DXGI_FORMAT_R32G32B32A32_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_R32G32B32A32_FLOAT);
        RT_CASE_RET_STR(DXGI_FORMAT_R32G32B32A32_UINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R32G32B32A32_SINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R32G32B32_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_R32G32B32_FLOAT);
        RT_CASE_RET_STR(DXGI_FORMAT_R32G32B32_UINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R32G32B32_SINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R16G16B16A16_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_R16G16B16A16_FLOAT);
        RT_CASE_RET_STR(DXGI_FORMAT_R16G16B16A16_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_R16G16B16A16_UINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R16G16B16A16_SNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_R16G16B16A16_SINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R32G32_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_R32G32_FLOAT);
        RT_CASE_RET_STR(DXGI_FORMAT_R32G32_UINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R32G32_SINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R32G8X24_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_D32_FLOAT_S8X24_UINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_X32_TYPELESS_G8X24_UINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R10G10B10A2_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_R10G10B10A2_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_R10G10B10A2_UINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R11G11B10_FLOAT);
        RT_CASE_RET_STR(DXGI_FORMAT_R8G8B8A8_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_R8G8B8A8_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        RT_CASE_RET_STR(DXGI_FORMAT_R8G8B8A8_UINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R8G8B8A8_SNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_R8G8B8A8_SINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R16G16_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_R16G16_FLOAT);
        RT_CASE_RET_STR(DXGI_FORMAT_R16G16_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_R16G16_UINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R16G16_SNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_R16G16_SINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R32_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_D32_FLOAT);
        RT_CASE_RET_STR(DXGI_FORMAT_R32_FLOAT);
        RT_CASE_RET_STR(DXGI_FORMAT_R32_UINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R32_SINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R24G8_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_D24_UNORM_S8_UINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_X24_TYPELESS_G8_UINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R8G8_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_R8G8_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_R8G8_UINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R8G8_SNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_R8G8_SINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R16_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_R16_FLOAT);
        RT_CASE_RET_STR(DXGI_FORMAT_D16_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_R16_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_R16_UINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R16_SNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_R16_SINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R8_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_R8_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_R8_UINT);
        RT_CASE_RET_STR(DXGI_FORMAT_R8_SNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_R8_SINT);
        RT_CASE_RET_STR(DXGI_FORMAT_A8_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_R1_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_R9G9B9E5_SHAREDEXP);
        RT_CASE_RET_STR(DXGI_FORMAT_R8G8_B8G8_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_G8R8_G8B8_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_BC1_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_BC1_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_BC1_UNORM_SRGB);
        RT_CASE_RET_STR(DXGI_FORMAT_BC2_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_BC2_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_BC2_UNORM_SRGB);
        RT_CASE_RET_STR(DXGI_FORMAT_BC3_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_BC3_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_BC3_UNORM_SRGB);
        RT_CASE_RET_STR(DXGI_FORMAT_BC4_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_BC4_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_BC4_SNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_BC5_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_BC5_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_BC5_SNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_B5G6R5_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_B5G5R5A1_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_B8G8R8A8_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_B8G8R8X8_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_B8G8R8A8_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
        RT_CASE_RET_STR(DXGI_FORMAT_B8G8R8X8_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_B8G8R8X8_UNORM_SRGB);
        RT_CASE_RET_STR(DXGI_FORMAT_BC6H_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_BC6H_UF16);
        RT_CASE_RET_STR(DXGI_FORMAT_BC6H_SF16);
        RT_CASE_RET_STR(DXGI_FORMAT_BC7_TYPELESS);
        RT_CASE_RET_STR(DXGI_FORMAT_BC7_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_BC7_UNORM_SRGB);
        RT_CASE_RET_STR(DXGI_FORMAT_AYUV);
        RT_CASE_RET_STR(DXGI_FORMAT_Y410);
        RT_CASE_RET_STR(DXGI_FORMAT_Y416);
        RT_CASE_RET_STR(DXGI_FORMAT_NV12);
        RT_CASE_RET_STR(DXGI_FORMAT_P010);
        RT_CASE_RET_STR(DXGI_FORMAT_P016);
        RT_CASE_RET_STR(DXGI_FORMAT_420_OPAQUE);
        RT_CASE_RET_STR(DXGI_FORMAT_YUY2);
        RT_CASE_RET_STR(DXGI_FORMAT_Y210);
        RT_CASE_RET_STR(DXGI_FORMAT_Y216);
        RT_CASE_RET_STR(DXGI_FORMAT_NV11);
        RT_CASE_RET_STR(DXGI_FORMAT_AI44);
        RT_CASE_RET_STR(DXGI_FORMAT_IA44);
        RT_CASE_RET_STR(DXGI_FORMAT_P8);
        RT_CASE_RET_STR(DXGI_FORMAT_A8P8);
        RT_CASE_RET_STR(DXGI_FORMAT_B4G4R4A4_UNORM);
        RT_CASE_RET_STR(DXGI_FORMAT_P208);
        RT_CASE_RET_STR(DXGI_FORMAT_V208);
        RT_CASE_RET_STR(DXGI_FORMAT_V408);
        default:
            break;
    }
    return "not known";
}
#endif /* LOG_ENABLED */

/* This is to workaround issues with X8 formats, because they can't be used in some operations. */
#define DX_REPLACE_X8_WITH_A8
static DXGI_FORMAT vmsvgaDXSurfaceFormat2Dxgi(SVGA3dSurfaceFormat format)
{
    /* Ensure that correct headers are used.
     * SVGA3D_AYUV was equal to 45, then replaced with SVGA3D_FORMAT_DEAD2 = 45, and redefined as SVGA3D_AYUV = 152.
     */
    AssertCompile(SVGA3D_AYUV == 152);

#define DXGI_FORMAT_ DXGI_FORMAT_UNKNOWN
    /** @todo More formats. */
    switch (format)
    {
#ifdef DX_REPLACE_X8_WITH_A8
        case SVGA3D_X8R8G8B8:                   return DXGI_FORMAT_B8G8R8A8_UNORM;
#else
        case SVGA3D_X8R8G8B8:                   return DXGI_FORMAT_B8G8R8X8_UNORM;
#endif
        case SVGA3D_A8R8G8B8:                   return DXGI_FORMAT_B8G8R8A8_UNORM;
        case SVGA3D_R5G6B5:                     return DXGI_FORMAT_B5G6R5_UNORM;
        case SVGA3D_X1R5G5B5:                   return DXGI_FORMAT_B5G5R5A1_UNORM;
        case SVGA3D_A1R5G5B5:                   return DXGI_FORMAT_B5G5R5A1_UNORM;
        case SVGA3D_A4R4G4B4:                   break; // 11.1 return DXGI_FORMAT_B4G4R4A4_UNORM;
        case SVGA3D_Z_D32:                      break;
        case SVGA3D_Z_D16:                      return DXGI_FORMAT_D16_UNORM;
        case SVGA3D_Z_D24S8:                    return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case SVGA3D_Z_D15S1:                    break;
        case SVGA3D_LUMINANCE8:                 return DXGI_FORMAT_;
        case SVGA3D_LUMINANCE4_ALPHA4:          return DXGI_FORMAT_;
        case SVGA3D_LUMINANCE16:                return DXGI_FORMAT_;
        case SVGA3D_LUMINANCE8_ALPHA8:          return DXGI_FORMAT_;
        case SVGA3D_DXT1:                       return DXGI_FORMAT_;
        case SVGA3D_DXT2:                       return DXGI_FORMAT_;
        case SVGA3D_DXT3:                       return DXGI_FORMAT_;
        case SVGA3D_DXT4:                       return DXGI_FORMAT_;
        case SVGA3D_DXT5:                       return DXGI_FORMAT_;
        case SVGA3D_BUMPU8V8:                   return DXGI_FORMAT_;
        case SVGA3D_BUMPL6V5U5:                 return DXGI_FORMAT_;
        case SVGA3D_BUMPX8L8V8U8:               return DXGI_FORMAT_;
        case SVGA3D_FORMAT_DEAD1:               break;
        case SVGA3D_ARGB_S10E5:                 return DXGI_FORMAT_;
        case SVGA3D_ARGB_S23E8:                 return DXGI_FORMAT_;
        case SVGA3D_A2R10G10B10:                return DXGI_FORMAT_;
        case SVGA3D_V8U8:                       return DXGI_FORMAT_;
        case SVGA3D_Q8W8V8U8:                   return DXGI_FORMAT_;
        case SVGA3D_CxV8U8:                     return DXGI_FORMAT_;
        case SVGA3D_X8L8V8U8:                   return DXGI_FORMAT_;
        case SVGA3D_A2W10V10U10:                return DXGI_FORMAT_;
        case SVGA3D_ALPHA8:                     return DXGI_FORMAT_;
        case SVGA3D_R_S10E5:                    return DXGI_FORMAT_;
        case SVGA3D_R_S23E8:                    return DXGI_FORMAT_;
        case SVGA3D_RG_S10E5:                   return DXGI_FORMAT_;
        case SVGA3D_RG_S23E8:                   return DXGI_FORMAT_;
        case SVGA3D_BUFFER:                     return DXGI_FORMAT_;
        case SVGA3D_Z_D24X8:                    return DXGI_FORMAT_;
        case SVGA3D_V16U16:                     return DXGI_FORMAT_;
        case SVGA3D_G16R16:                     return DXGI_FORMAT_;
        case SVGA3D_A16B16G16R16:               return DXGI_FORMAT_;
        case SVGA3D_UYVY:                       return DXGI_FORMAT_;
        case SVGA3D_YUY2:                       return DXGI_FORMAT_YUY2;
        case SVGA3D_NV12:                       return DXGI_FORMAT_NV12;
        case SVGA3D_FORMAT_DEAD2:               break; /* Old SVGA3D_AYUV */
        case SVGA3D_R32G32B32A32_TYPELESS:      return DXGI_FORMAT_R32G32B32A32_TYPELESS;
        case SVGA3D_R32G32B32A32_UINT:          return DXGI_FORMAT_R32G32B32A32_UINT;
        case SVGA3D_R32G32B32A32_SINT:          return DXGI_FORMAT_R32G32B32A32_SINT;
        case SVGA3D_R32G32B32_TYPELESS:         return DXGI_FORMAT_R32G32B32_TYPELESS;
        case SVGA3D_R32G32B32_FLOAT:            return DXGI_FORMAT_R32G32B32_FLOAT;
        case SVGA3D_R32G32B32_UINT:             return DXGI_FORMAT_R32G32B32_UINT;
        case SVGA3D_R32G32B32_SINT:             return DXGI_FORMAT_R32G32B32_SINT;
        case SVGA3D_R16G16B16A16_TYPELESS:      return DXGI_FORMAT_R16G16B16A16_TYPELESS;
        case SVGA3D_R16G16B16A16_UINT:          return DXGI_FORMAT_R16G16B16A16_UINT;
        case SVGA3D_R16G16B16A16_SNORM:         return DXGI_FORMAT_R16G16B16A16_SNORM;
        case SVGA3D_R16G16B16A16_SINT:          return DXGI_FORMAT_R16G16B16A16_SINT;
        case SVGA3D_R32G32_TYPELESS:            return DXGI_FORMAT_R32G32_TYPELESS;
        case SVGA3D_R32G32_UINT:                return DXGI_FORMAT_R32G32_UINT;
        case SVGA3D_R32G32_SINT:                return DXGI_FORMAT_R32G32_SINT;
        case SVGA3D_R32G8X24_TYPELESS:          return DXGI_FORMAT_R32G8X24_TYPELESS;
        case SVGA3D_D32_FLOAT_S8X24_UINT:       return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        case SVGA3D_R32_FLOAT_X8X24:            return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        case SVGA3D_X32_G8X24_UINT:             return DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;
        case SVGA3D_R10G10B10A2_TYPELESS:       return DXGI_FORMAT_R10G10B10A2_TYPELESS;
        case SVGA3D_R10G10B10A2_UINT:           return DXGI_FORMAT_R10G10B10A2_UINT;
        case SVGA3D_R11G11B10_FLOAT:            return DXGI_FORMAT_R11G11B10_FLOAT;
        case SVGA3D_R8G8B8A8_TYPELESS:          return DXGI_FORMAT_R8G8B8A8_TYPELESS;
        case SVGA3D_R8G8B8A8_UNORM:             return DXGI_FORMAT_R8G8B8A8_UNORM;
        case SVGA3D_R8G8B8A8_UNORM_SRGB:        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case SVGA3D_R8G8B8A8_UINT:              return DXGI_FORMAT_R8G8B8A8_UINT;
        case SVGA3D_R8G8B8A8_SINT:              return DXGI_FORMAT_R8G8B8A8_SINT;
        case SVGA3D_R16G16_TYPELESS:            return DXGI_FORMAT_R16G16_TYPELESS;
        case SVGA3D_R16G16_UINT:                return DXGI_FORMAT_R16G16_UINT;
        case SVGA3D_R16G16_SINT:                return DXGI_FORMAT_R16G16_SINT;
        case SVGA3D_R32_TYPELESS:               return DXGI_FORMAT_R32_TYPELESS;
        case SVGA3D_D32_FLOAT:                  return DXGI_FORMAT_D32_FLOAT;
        case SVGA3D_R32_UINT:                   return DXGI_FORMAT_R32_UINT;
        case SVGA3D_R32_SINT:                   return DXGI_FORMAT_R32_SINT;
        case SVGA3D_R24G8_TYPELESS:             return DXGI_FORMAT_R24G8_TYPELESS;
        case SVGA3D_D24_UNORM_S8_UINT:          return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case SVGA3D_R24_UNORM_X8:               return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case SVGA3D_X24_G8_UINT:                return DXGI_FORMAT_X24_TYPELESS_G8_UINT;
        case SVGA3D_R8G8_TYPELESS:              return DXGI_FORMAT_R8G8_TYPELESS;
        case SVGA3D_R8G8_UNORM:                 return DXGI_FORMAT_R8G8_UNORM;
        case SVGA3D_R8G8_UINT:                  return DXGI_FORMAT_R8G8_UINT;
        case SVGA3D_R8G8_SINT:                  return DXGI_FORMAT_R8G8_SINT;
        case SVGA3D_R16_TYPELESS:               return DXGI_FORMAT_R16_TYPELESS;
        case SVGA3D_R16_UNORM:                  return DXGI_FORMAT_R16_UNORM;
        case SVGA3D_R16_UINT:                   return DXGI_FORMAT_R16_UINT;
        case SVGA3D_R16_SNORM:                  return DXGI_FORMAT_R16_SNORM;
        case SVGA3D_R16_SINT:                   return DXGI_FORMAT_R16_SINT;
        case SVGA3D_R8_TYPELESS:                return DXGI_FORMAT_R8_TYPELESS;
        case SVGA3D_R8_UNORM:                   return DXGI_FORMAT_R8_UNORM;
        case SVGA3D_R8_UINT:                    return DXGI_FORMAT_R8_UINT;
        case SVGA3D_R8_SNORM:                   return DXGI_FORMAT_R8_SNORM;
        case SVGA3D_R8_SINT:                    return DXGI_FORMAT_R8_SINT;
        case SVGA3D_P8:                         break;
        case SVGA3D_R9G9B9E5_SHAREDEXP:         return DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
        case SVGA3D_R8G8_B8G8_UNORM:            return DXGI_FORMAT_R8G8_B8G8_UNORM;
        case SVGA3D_G8R8_G8B8_UNORM:            return DXGI_FORMAT_G8R8_G8B8_UNORM;
        case SVGA3D_BC1_TYPELESS:               return DXGI_FORMAT_BC1_TYPELESS;
        case SVGA3D_BC1_UNORM_SRGB:             return DXGI_FORMAT_BC1_UNORM_SRGB;
        case SVGA3D_BC2_TYPELESS:               return DXGI_FORMAT_BC2_TYPELESS;
        case SVGA3D_BC2_UNORM_SRGB:             return DXGI_FORMAT_BC2_UNORM_SRGB;
        case SVGA3D_BC3_TYPELESS:               return DXGI_FORMAT_BC3_TYPELESS;
        case SVGA3D_BC3_UNORM_SRGB:             return DXGI_FORMAT_BC3_UNORM_SRGB;
        case SVGA3D_BC4_TYPELESS:               return DXGI_FORMAT_BC4_TYPELESS;
        case SVGA3D_ATI1:                       break;
        case SVGA3D_BC4_SNORM:                  return DXGI_FORMAT_BC4_SNORM;
        case SVGA3D_BC5_TYPELESS:               return DXGI_FORMAT_BC5_TYPELESS;
        case SVGA3D_ATI2:                       break;
        case SVGA3D_BC5_SNORM:                  return DXGI_FORMAT_BC5_SNORM;
        case SVGA3D_R10G10B10_XR_BIAS_A2_UNORM: return DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM;
        case SVGA3D_B8G8R8A8_TYPELESS:          return DXGI_FORMAT_B8G8R8A8_TYPELESS;
        case SVGA3D_B8G8R8A8_UNORM_SRGB:        return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
#ifdef DX_REPLACE_X8_WITH_A8
        case SVGA3D_B8G8R8X8_TYPELESS:          return DXGI_FORMAT_B8G8R8A8_TYPELESS;
        case SVGA3D_B8G8R8X8_UNORM_SRGB:        return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
#else
        case SVGA3D_B8G8R8X8_TYPELESS:          return DXGI_FORMAT_B8G8R8X8_TYPELESS;
        case SVGA3D_B8G8R8X8_UNORM_SRGB:        return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
#endif
        case SVGA3D_Z_DF16:                     break;
        case SVGA3D_Z_DF24:                     break;
        case SVGA3D_Z_D24S8_INT:                return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case SVGA3D_YV12:                       break;
        case SVGA3D_R32G32B32A32_FLOAT:         return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case SVGA3D_R16G16B16A16_FLOAT:         return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case SVGA3D_R16G16B16A16_UNORM:         return DXGI_FORMAT_R16G16B16A16_UNORM;
        case SVGA3D_R32G32_FLOAT:               return DXGI_FORMAT_R32G32_FLOAT;
        case SVGA3D_R10G10B10A2_UNORM:          return DXGI_FORMAT_R10G10B10A2_UNORM;
        case SVGA3D_R8G8B8A8_SNORM:             return DXGI_FORMAT_R8G8B8A8_SNORM;
        case SVGA3D_R16G16_FLOAT:               return DXGI_FORMAT_R16G16_FLOAT;
        case SVGA3D_R16G16_UNORM:               return DXGI_FORMAT_R16G16_UNORM;
        case SVGA3D_R16G16_SNORM:               return DXGI_FORMAT_R16G16_SNORM;
        case SVGA3D_R32_FLOAT:                  return DXGI_FORMAT_R32_FLOAT;
        case SVGA3D_R8G8_SNORM:                 return DXGI_FORMAT_R8G8_SNORM;
        case SVGA3D_R16_FLOAT:                  return DXGI_FORMAT_R16_FLOAT;
        case SVGA3D_D16_UNORM:                  return DXGI_FORMAT_D16_UNORM;
        case SVGA3D_A8_UNORM:                   return DXGI_FORMAT_A8_UNORM;
        case SVGA3D_BC1_UNORM:                  return DXGI_FORMAT_BC1_UNORM;
        case SVGA3D_BC2_UNORM:                  return DXGI_FORMAT_BC2_UNORM;
        case SVGA3D_BC3_UNORM:                  return DXGI_FORMAT_BC3_UNORM;
        case SVGA3D_B5G6R5_UNORM:               return DXGI_FORMAT_B5G6R5_UNORM;
        case SVGA3D_B5G5R5A1_UNORM:             return DXGI_FORMAT_B5G5R5A1_UNORM;
        case SVGA3D_B8G8R8A8_UNORM:             return DXGI_FORMAT_B8G8R8A8_UNORM;
#ifdef DX_REPLACE_X8_WITH_A8
        case SVGA3D_B8G8R8X8_UNORM:             return DXGI_FORMAT_B8G8R8A8_UNORM;
#else
        case SVGA3D_B8G8R8X8_UNORM:             return DXGI_FORMAT_B8G8R8X8_UNORM;
#endif
        case SVGA3D_BC4_UNORM:                  return DXGI_FORMAT_BC4_UNORM;
        case SVGA3D_BC5_UNORM:                  return DXGI_FORMAT_BC5_UNORM;

        case SVGA3D_B4G4R4A4_UNORM:             return DXGI_FORMAT_;
        case SVGA3D_BC6H_TYPELESS:              return DXGI_FORMAT_BC6H_TYPELESS;
        case SVGA3D_BC6H_UF16:                  return DXGI_FORMAT_BC6H_UF16;
        case SVGA3D_BC6H_SF16:                  return DXGI_FORMAT_BC6H_SF16;
        case SVGA3D_BC7_TYPELESS:               return DXGI_FORMAT_BC7_TYPELESS;
        case SVGA3D_BC7_UNORM:                  return DXGI_FORMAT_BC7_UNORM;
        case SVGA3D_BC7_UNORM_SRGB:             return DXGI_FORMAT_BC7_UNORM_SRGB;
        case SVGA3D_AYUV:                       return DXGI_FORMAT_AYUV;

        case SVGA3D_FORMAT_INVALID:
        case SVGA3D_FORMAT_MAX:                 break;
    }
    // AssertFailed();
    return DXGI_FORMAT_UNKNOWN;
#undef DXGI_FORMAT_
}


static SVGA3dSurfaceFormat vmsvgaDXDevCapSurfaceFmt2Format(SVGA3dDevCapIndex enmDevCap)
{
    switch (enmDevCap)
    {
        case SVGA3D_DEVCAP_SURFACEFMT_X8R8G8B8:                 return SVGA3D_X8R8G8B8;
        case SVGA3D_DEVCAP_SURFACEFMT_A8R8G8B8:                 return SVGA3D_A8R8G8B8;
        case SVGA3D_DEVCAP_SURFACEFMT_A2R10G10B10:              return SVGA3D_A2R10G10B10;
        case SVGA3D_DEVCAP_SURFACEFMT_X1R5G5B5:                 return SVGA3D_X1R5G5B5;
        case SVGA3D_DEVCAP_SURFACEFMT_A1R5G5B5:                 return SVGA3D_A1R5G5B5;
        case SVGA3D_DEVCAP_SURFACEFMT_A4R4G4B4:                 return SVGA3D_A4R4G4B4;
        case SVGA3D_DEVCAP_SURFACEFMT_R5G6B5:                   return SVGA3D_R5G6B5;
        case SVGA3D_DEVCAP_SURFACEFMT_LUMINANCE16:              return SVGA3D_LUMINANCE16;
        case SVGA3D_DEVCAP_SURFACEFMT_LUMINANCE8_ALPHA8:        return SVGA3D_LUMINANCE8_ALPHA8;
        case SVGA3D_DEVCAP_SURFACEFMT_ALPHA8:                   return SVGA3D_ALPHA8;
        case SVGA3D_DEVCAP_SURFACEFMT_LUMINANCE8:               return SVGA3D_LUMINANCE8;
        case SVGA3D_DEVCAP_SURFACEFMT_Z_D16:                    return SVGA3D_Z_D16;
        case SVGA3D_DEVCAP_SURFACEFMT_Z_D24S8:                  return SVGA3D_Z_D24S8;
        case SVGA3D_DEVCAP_SURFACEFMT_Z_D24X8:                  return SVGA3D_Z_D24X8;
        case SVGA3D_DEVCAP_SURFACEFMT_DXT1:                     return SVGA3D_DXT1;
        case SVGA3D_DEVCAP_SURFACEFMT_DXT2:                     return SVGA3D_DXT2;
        case SVGA3D_DEVCAP_SURFACEFMT_DXT3:                     return SVGA3D_DXT3;
        case SVGA3D_DEVCAP_SURFACEFMT_DXT4:                     return SVGA3D_DXT4;
        case SVGA3D_DEVCAP_SURFACEFMT_DXT5:                     return SVGA3D_DXT5;
        case SVGA3D_DEVCAP_SURFACEFMT_BUMPX8L8V8U8:             return SVGA3D_BUMPX8L8V8U8;
        case SVGA3D_DEVCAP_SURFACEFMT_A2W10V10U10:              return SVGA3D_A2W10V10U10;
        case SVGA3D_DEVCAP_SURFACEFMT_BUMPU8V8:                 return SVGA3D_BUMPU8V8;
        case SVGA3D_DEVCAP_SURFACEFMT_Q8W8V8U8:                 return SVGA3D_Q8W8V8U8;
        case SVGA3D_DEVCAP_SURFACEFMT_CxV8U8:                   return SVGA3D_CxV8U8;
        case SVGA3D_DEVCAP_SURFACEFMT_R_S10E5:                  return SVGA3D_R_S10E5;
        case SVGA3D_DEVCAP_SURFACEFMT_R_S23E8:                  return SVGA3D_R_S23E8;
        case SVGA3D_DEVCAP_SURFACEFMT_RG_S10E5:                 return SVGA3D_RG_S10E5;
        case SVGA3D_DEVCAP_SURFACEFMT_RG_S23E8:                 return SVGA3D_RG_S23E8;
        case SVGA3D_DEVCAP_SURFACEFMT_ARGB_S10E5:               return SVGA3D_ARGB_S10E5;
        case SVGA3D_DEVCAP_SURFACEFMT_ARGB_S23E8:               return SVGA3D_ARGB_S23E8;
        case SVGA3D_DEVCAP_SURFACEFMT_V16U16:                   return SVGA3D_V16U16;
        case SVGA3D_DEVCAP_SURFACEFMT_G16R16:                   return SVGA3D_G16R16;
        case SVGA3D_DEVCAP_SURFACEFMT_A16B16G16R16:             return SVGA3D_A16B16G16R16;
        case SVGA3D_DEVCAP_SURFACEFMT_UYVY:                     return SVGA3D_UYVY;
        case SVGA3D_DEVCAP_SURFACEFMT_YUY2:                     return SVGA3D_YUY2;
        case SVGA3D_DEVCAP_SURFACEFMT_NV12:                     return SVGA3D_NV12;
        case SVGA3D_DEVCAP_DEAD10:                              return SVGA3D_FORMAT_DEAD2; /* SVGA3D_DEVCAP_SURFACEFMT_AYUV -> SVGA3D_AYUV */
        case SVGA3D_DEVCAP_SURFACEFMT_Z_DF16:                   return SVGA3D_Z_DF16;
        case SVGA3D_DEVCAP_SURFACEFMT_Z_DF24:                   return SVGA3D_Z_DF24;
        case SVGA3D_DEVCAP_SURFACEFMT_Z_D24S8_INT:              return SVGA3D_Z_D24S8_INT;
        case SVGA3D_DEVCAP_SURFACEFMT_ATI1:                     return SVGA3D_ATI1;
        case SVGA3D_DEVCAP_SURFACEFMT_ATI2:                     return SVGA3D_ATI2;
        case SVGA3D_DEVCAP_SURFACEFMT_YV12:                     return SVGA3D_YV12;
        default:
            AssertFailed();
            break;
    }
    return SVGA3D_FORMAT_INVALID;
}


static SVGA3dSurfaceFormat vmsvgaDXDevCapDxfmt2Format(SVGA3dDevCapIndex enmDevCap)
{
    switch (enmDevCap)
    {
        case SVGA3D_DEVCAP_DXFMT_X8R8G8B8:                      return SVGA3D_X8R8G8B8;
        case SVGA3D_DEVCAP_DXFMT_A8R8G8B8:                      return SVGA3D_A8R8G8B8;
        case SVGA3D_DEVCAP_DXFMT_R5G6B5:                        return SVGA3D_R5G6B5;
        case SVGA3D_DEVCAP_DXFMT_X1R5G5B5:                      return SVGA3D_X1R5G5B5;
        case SVGA3D_DEVCAP_DXFMT_A1R5G5B5:                      return SVGA3D_A1R5G5B5;
        case SVGA3D_DEVCAP_DXFMT_A4R4G4B4:                      return SVGA3D_A4R4G4B4;
        case SVGA3D_DEVCAP_DXFMT_Z_D32:                         return SVGA3D_Z_D32;
        case SVGA3D_DEVCAP_DXFMT_Z_D16:                         return SVGA3D_Z_D16;
        case SVGA3D_DEVCAP_DXFMT_Z_D24S8:                       return SVGA3D_Z_D24S8;
        case SVGA3D_DEVCAP_DXFMT_Z_D15S1:                       return SVGA3D_Z_D15S1;
        case SVGA3D_DEVCAP_DXFMT_LUMINANCE8:                    return SVGA3D_LUMINANCE8;
        case SVGA3D_DEVCAP_DXFMT_LUMINANCE4_ALPHA4:             return SVGA3D_LUMINANCE4_ALPHA4;
        case SVGA3D_DEVCAP_DXFMT_LUMINANCE16:                   return SVGA3D_LUMINANCE16;
        case SVGA3D_DEVCAP_DXFMT_LUMINANCE8_ALPHA8:             return SVGA3D_LUMINANCE8_ALPHA8;
        case SVGA3D_DEVCAP_DXFMT_DXT1:                          return SVGA3D_DXT1;
        case SVGA3D_DEVCAP_DXFMT_DXT2:                          return SVGA3D_DXT2;
        case SVGA3D_DEVCAP_DXFMT_DXT3:                          return SVGA3D_DXT3;
        case SVGA3D_DEVCAP_DXFMT_DXT4:                          return SVGA3D_DXT4;
        case SVGA3D_DEVCAP_DXFMT_DXT5:                          return SVGA3D_DXT5;
        case SVGA3D_DEVCAP_DXFMT_BUMPU8V8:                      return SVGA3D_BUMPU8V8;
        case SVGA3D_DEVCAP_DXFMT_BUMPL6V5U5:                    return SVGA3D_BUMPL6V5U5;
        case SVGA3D_DEVCAP_DXFMT_BUMPX8L8V8U8:                  return SVGA3D_BUMPX8L8V8U8;
        case SVGA3D_DEVCAP_DXFMT_FORMAT_DEAD1:                  return SVGA3D_FORMAT_DEAD1;
        case SVGA3D_DEVCAP_DXFMT_ARGB_S10E5:                    return SVGA3D_ARGB_S10E5;
        case SVGA3D_DEVCAP_DXFMT_ARGB_S23E8:                    return SVGA3D_ARGB_S23E8;
        case SVGA3D_DEVCAP_DXFMT_A2R10G10B10:                   return SVGA3D_A2R10G10B10;
        case SVGA3D_DEVCAP_DXFMT_V8U8:                          return SVGA3D_V8U8;
        case SVGA3D_DEVCAP_DXFMT_Q8W8V8U8:                      return SVGA3D_Q8W8V8U8;
        case SVGA3D_DEVCAP_DXFMT_CxV8U8:                        return SVGA3D_CxV8U8;
        case SVGA3D_DEVCAP_DXFMT_X8L8V8U8:                      return SVGA3D_X8L8V8U8;
        case SVGA3D_DEVCAP_DXFMT_A2W10V10U10:                   return SVGA3D_A2W10V10U10;
        case SVGA3D_DEVCAP_DXFMT_ALPHA8:                        return SVGA3D_ALPHA8;
        case SVGA3D_DEVCAP_DXFMT_R_S10E5:                       return SVGA3D_R_S10E5;
        case SVGA3D_DEVCAP_DXFMT_R_S23E8:                       return SVGA3D_R_S23E8;
        case SVGA3D_DEVCAP_DXFMT_RG_S10E5:                      return SVGA3D_RG_S10E5;
        case SVGA3D_DEVCAP_DXFMT_RG_S23E8:                      return SVGA3D_RG_S23E8;
        case SVGA3D_DEVCAP_DXFMT_BUFFER:                        return SVGA3D_BUFFER;
        case SVGA3D_DEVCAP_DXFMT_Z_D24X8:                       return SVGA3D_Z_D24X8;
        case SVGA3D_DEVCAP_DXFMT_V16U16:                        return SVGA3D_V16U16;
        case SVGA3D_DEVCAP_DXFMT_G16R16:                        return SVGA3D_G16R16;
        case SVGA3D_DEVCAP_DXFMT_A16B16G16R16:                  return SVGA3D_A16B16G16R16;
        case SVGA3D_DEVCAP_DXFMT_UYVY:                          return SVGA3D_UYVY;
        case SVGA3D_DEVCAP_DXFMT_YUY2:                          return SVGA3D_YUY2;
        case SVGA3D_DEVCAP_DXFMT_NV12:                          return SVGA3D_NV12;
        case SVGA3D_DEVCAP_DXFMT_FORMAT_DEAD2:                  return SVGA3D_FORMAT_DEAD2; /* SVGA3D_DEVCAP_DXFMT_AYUV -> SVGA3D_AYUV */
        case SVGA3D_DEVCAP_DXFMT_R32G32B32A32_TYPELESS:         return SVGA3D_R32G32B32A32_TYPELESS;
        case SVGA3D_DEVCAP_DXFMT_R32G32B32A32_UINT:             return SVGA3D_R32G32B32A32_UINT;
        case SVGA3D_DEVCAP_DXFMT_R32G32B32A32_SINT:             return SVGA3D_R32G32B32A32_SINT;
        case SVGA3D_DEVCAP_DXFMT_R32G32B32_TYPELESS:            return SVGA3D_R32G32B32_TYPELESS;
        case SVGA3D_DEVCAP_DXFMT_R32G32B32_FLOAT:               return SVGA3D_R32G32B32_FLOAT;
        case SVGA3D_DEVCAP_DXFMT_R32G32B32_UINT:                return SVGA3D_R32G32B32_UINT;
        case SVGA3D_DEVCAP_DXFMT_R32G32B32_SINT:                return SVGA3D_R32G32B32_SINT;
        case SVGA3D_DEVCAP_DXFMT_R16G16B16A16_TYPELESS:         return SVGA3D_R16G16B16A16_TYPELESS;
        case SVGA3D_DEVCAP_DXFMT_R16G16B16A16_UINT:             return SVGA3D_R16G16B16A16_UINT;
        case SVGA3D_DEVCAP_DXFMT_R16G16B16A16_SNORM:            return SVGA3D_R16G16B16A16_SNORM;
        case SVGA3D_DEVCAP_DXFMT_R16G16B16A16_SINT:             return SVGA3D_R16G16B16A16_SINT;
        case SVGA3D_DEVCAP_DXFMT_R32G32_TYPELESS:               return SVGA3D_R32G32_TYPELESS;
        case SVGA3D_DEVCAP_DXFMT_R32G32_UINT:                   return SVGA3D_R32G32_UINT;
        case SVGA3D_DEVCAP_DXFMT_R32G32_SINT:                   return SVGA3D_R32G32_SINT;
        case SVGA3D_DEVCAP_DXFMT_R32G8X24_TYPELESS:             return SVGA3D_R32G8X24_TYPELESS;
        case SVGA3D_DEVCAP_DXFMT_D32_FLOAT_S8X24_UINT:          return SVGA3D_D32_FLOAT_S8X24_UINT;
        case SVGA3D_DEVCAP_DXFMT_R32_FLOAT_X8X24:               return SVGA3D_R32_FLOAT_X8X24;
        case SVGA3D_DEVCAP_DXFMT_X32_G8X24_UINT:                return SVGA3D_X32_G8X24_UINT;
        case SVGA3D_DEVCAP_DXFMT_R10G10B10A2_TYPELESS:          return SVGA3D_R10G10B10A2_TYPELESS;
        case SVGA3D_DEVCAP_DXFMT_R10G10B10A2_UINT:              return SVGA3D_R10G10B10A2_UINT;
        case SVGA3D_DEVCAP_DXFMT_R11G11B10_FLOAT:               return SVGA3D_R11G11B10_FLOAT;
        case SVGA3D_DEVCAP_DXFMT_R8G8B8A8_TYPELESS:             return SVGA3D_R8G8B8A8_TYPELESS;
        case SVGA3D_DEVCAP_DXFMT_R8G8B8A8_UNORM:                return SVGA3D_R8G8B8A8_UNORM;
        case SVGA3D_DEVCAP_DXFMT_R8G8B8A8_UNORM_SRGB:           return SVGA3D_R8G8B8A8_UNORM_SRGB;
        case SVGA3D_DEVCAP_DXFMT_R8G8B8A8_UINT:                 return SVGA3D_R8G8B8A8_UINT;
        case SVGA3D_DEVCAP_DXFMT_R8G8B8A8_SINT:                 return SVGA3D_R8G8B8A8_SINT;
        case SVGA3D_DEVCAP_DXFMT_R16G16_TYPELESS:               return SVGA3D_R16G16_TYPELESS;
        case SVGA3D_DEVCAP_DXFMT_R16G16_UINT:                   return SVGA3D_R16G16_UINT;
        case SVGA3D_DEVCAP_DXFMT_R16G16_SINT:                   return SVGA3D_R16G16_SINT;
        case SVGA3D_DEVCAP_DXFMT_R32_TYPELESS:                  return SVGA3D_R32_TYPELESS;
        case SVGA3D_DEVCAP_DXFMT_D32_FLOAT:                     return SVGA3D_D32_FLOAT;
        case SVGA3D_DEVCAP_DXFMT_R32_UINT:                      return SVGA3D_R32_UINT;
        case SVGA3D_DEVCAP_DXFMT_R32_SINT:                      return SVGA3D_R32_SINT;
        case SVGA3D_DEVCAP_DXFMT_R24G8_TYPELESS:                return SVGA3D_R24G8_TYPELESS;
        case SVGA3D_DEVCAP_DXFMT_D24_UNORM_S8_UINT:             return SVGA3D_D24_UNORM_S8_UINT;
        case SVGA3D_DEVCAP_DXFMT_R24_UNORM_X8:                  return SVGA3D_R24_UNORM_X8;
        case SVGA3D_DEVCAP_DXFMT_X24_G8_UINT:                   return SVGA3D_X24_G8_UINT;
        case SVGA3D_DEVCAP_DXFMT_R8G8_TYPELESS:                 return SVGA3D_R8G8_TYPELESS;
        case SVGA3D_DEVCAP_DXFMT_R8G8_UNORM:                    return SVGA3D_R8G8_UNORM;
        case SVGA3D_DEVCAP_DXFMT_R8G8_UINT:                     return SVGA3D_R8G8_UINT;
        case SVGA3D_DEVCAP_DXFMT_R8G8_SINT:                     return SVGA3D_R8G8_SINT;
        case SVGA3D_DEVCAP_DXFMT_R16_TYPELESS:                  return SVGA3D_R16_TYPELESS;
        case SVGA3D_DEVCAP_DXFMT_R16_UNORM:                     return SVGA3D_R16_UNORM;
        case SVGA3D_DEVCAP_DXFMT_R16_UINT:                      return SVGA3D_R16_UINT;
        case SVGA3D_DEVCAP_DXFMT_R16_SNORM:                     return SVGA3D_R16_SNORM;
        case SVGA3D_DEVCAP_DXFMT_R16_SINT:                      return SVGA3D_R16_SINT;
        case SVGA3D_DEVCAP_DXFMT_R8_TYPELESS:                   return SVGA3D_R8_TYPELESS;
        case SVGA3D_DEVCAP_DXFMT_R8_UNORM:                      return SVGA3D_R8_UNORM;
        case SVGA3D_DEVCAP_DXFMT_R8_UINT:                       return SVGA3D_R8_UINT;
        case SVGA3D_DEVCAP_DXFMT_R8_SNORM:                      return SVGA3D_R8_SNORM;
        case SVGA3D_DEVCAP_DXFMT_R8_SINT:                       return SVGA3D_R8_SINT;
        case SVGA3D_DEVCAP_DXFMT_P8:                            return SVGA3D_P8;
        case SVGA3D_DEVCAP_DXFMT_R9G9B9E5_SHAREDEXP:            return SVGA3D_R9G9B9E5_SHAREDEXP;
        case SVGA3D_DEVCAP_DXFMT_R8G8_B8G8_UNORM:               return SVGA3D_R8G8_B8G8_UNORM;
        case SVGA3D_DEVCAP_DXFMT_G8R8_G8B8_UNORM:               return SVGA3D_G8R8_G8B8_UNORM;
        case SVGA3D_DEVCAP_DXFMT_BC1_TYPELESS:                  return SVGA3D_BC1_TYPELESS;
        case SVGA3D_DEVCAP_DXFMT_BC1_UNORM_SRGB:                return SVGA3D_BC1_UNORM_SRGB;
        case SVGA3D_DEVCAP_DXFMT_BC2_TYPELESS:                  return SVGA3D_BC2_TYPELESS;
        case SVGA3D_DEVCAP_DXFMT_BC2_UNORM_SRGB:                return SVGA3D_BC2_UNORM_SRGB;
        case SVGA3D_DEVCAP_DXFMT_BC3_TYPELESS:                  return SVGA3D_BC3_TYPELESS;
        case SVGA3D_DEVCAP_DXFMT_BC3_UNORM_SRGB:                return SVGA3D_BC3_UNORM_SRGB;
        case SVGA3D_DEVCAP_DXFMT_BC4_TYPELESS:                  return SVGA3D_BC4_TYPELESS;
        case SVGA3D_DEVCAP_DXFMT_ATI1:                          return SVGA3D_ATI1;
        case SVGA3D_DEVCAP_DXFMT_BC4_SNORM:                     return SVGA3D_BC4_SNORM;
        case SVGA3D_DEVCAP_DXFMT_BC5_TYPELESS:                  return SVGA3D_BC5_TYPELESS;
        case SVGA3D_DEVCAP_DXFMT_ATI2:                          return SVGA3D_ATI2;
        case SVGA3D_DEVCAP_DXFMT_BC5_SNORM:                     return SVGA3D_BC5_SNORM;
        case SVGA3D_DEVCAP_DXFMT_R10G10B10_XR_BIAS_A2_UNORM:    return SVGA3D_R10G10B10_XR_BIAS_A2_UNORM;
        case SVGA3D_DEVCAP_DXFMT_B8G8R8A8_TYPELESS:             return SVGA3D_B8G8R8A8_TYPELESS;
        case SVGA3D_DEVCAP_DXFMT_B8G8R8A8_UNORM_SRGB:           return SVGA3D_B8G8R8A8_UNORM_SRGB;
        case SVGA3D_DEVCAP_DXFMT_B8G8R8X8_TYPELESS:             return SVGA3D_B8G8R8X8_TYPELESS;
        case SVGA3D_DEVCAP_DXFMT_B8G8R8X8_UNORM_SRGB:           return SVGA3D_B8G8R8X8_UNORM_SRGB;
        case SVGA3D_DEVCAP_DXFMT_Z_DF16:                        return SVGA3D_Z_DF16;
        case SVGA3D_DEVCAP_DXFMT_Z_DF24:                        return SVGA3D_Z_DF24;
        case SVGA3D_DEVCAP_DXFMT_Z_D24S8_INT:                   return SVGA3D_Z_D24S8_INT;
        case SVGA3D_DEVCAP_DXFMT_YV12:                          return SVGA3D_YV12;
        case SVGA3D_DEVCAP_DXFMT_R32G32B32A32_FLOAT:            return SVGA3D_R32G32B32A32_FLOAT;
        case SVGA3D_DEVCAP_DXFMT_R16G16B16A16_FLOAT:            return SVGA3D_R16G16B16A16_FLOAT;
        case SVGA3D_DEVCAP_DXFMT_R16G16B16A16_UNORM:            return SVGA3D_R16G16B16A16_UNORM;
        case SVGA3D_DEVCAP_DXFMT_R32G32_FLOAT:                  return SVGA3D_R32G32_FLOAT;
        case SVGA3D_DEVCAP_DXFMT_R10G10B10A2_UNORM:             return SVGA3D_R10G10B10A2_UNORM;
        case SVGA3D_DEVCAP_DXFMT_R8G8B8A8_SNORM:                return SVGA3D_R8G8B8A8_SNORM;
        case SVGA3D_DEVCAP_DXFMT_R16G16_FLOAT:                  return SVGA3D_R16G16_FLOAT;
        case SVGA3D_DEVCAP_DXFMT_R16G16_UNORM:                  return SVGA3D_R16G16_UNORM;
        case SVGA3D_DEVCAP_DXFMT_R16G16_SNORM:                  return SVGA3D_R16G16_SNORM;
        case SVGA3D_DEVCAP_DXFMT_R32_FLOAT:                     return SVGA3D_R32_FLOAT;
        case SVGA3D_DEVCAP_DXFMT_R8G8_SNORM:                    return SVGA3D_R8G8_SNORM;
        case SVGA3D_DEVCAP_DXFMT_R16_FLOAT:                     return SVGA3D_R16_FLOAT;
        case SVGA3D_DEVCAP_DXFMT_D16_UNORM:                     return SVGA3D_D16_UNORM;
        case SVGA3D_DEVCAP_DXFMT_A8_UNORM:                      return SVGA3D_A8_UNORM;
        case SVGA3D_DEVCAP_DXFMT_BC1_UNORM:                     return SVGA3D_BC1_UNORM;
        case SVGA3D_DEVCAP_DXFMT_BC2_UNORM:                     return SVGA3D_BC2_UNORM;
        case SVGA3D_DEVCAP_DXFMT_BC3_UNORM:                     return SVGA3D_BC3_UNORM;
        case SVGA3D_DEVCAP_DXFMT_B5G6R5_UNORM:                  return SVGA3D_B5G6R5_UNORM;
        case SVGA3D_DEVCAP_DXFMT_B5G5R5A1_UNORM:                return SVGA3D_B5G5R5A1_UNORM;
        case SVGA3D_DEVCAP_DXFMT_B8G8R8A8_UNORM:                return SVGA3D_B8G8R8A8_UNORM;
        case SVGA3D_DEVCAP_DXFMT_B8G8R8X8_UNORM:                return SVGA3D_B8G8R8X8_UNORM;
        case SVGA3D_DEVCAP_DXFMT_BC4_UNORM:                     return SVGA3D_BC4_UNORM;
        case SVGA3D_DEVCAP_DXFMT_BC5_UNORM:                     return SVGA3D_BC5_UNORM;
        case SVGA3D_DEVCAP_DXFMT_BC6H_TYPELESS:                 return SVGA3D_BC6H_TYPELESS;
        case SVGA3D_DEVCAP_DXFMT_BC6H_UF16:                     return SVGA3D_BC6H_UF16;
        case SVGA3D_DEVCAP_DXFMT_BC6H_SF16:                     return SVGA3D_BC6H_SF16;
        case SVGA3D_DEVCAP_DXFMT_BC7_TYPELESS:                  return SVGA3D_BC7_TYPELESS;
        case SVGA3D_DEVCAP_DXFMT_BC7_UNORM:                     return SVGA3D_BC7_UNORM;
        case SVGA3D_DEVCAP_DXFMT_BC7_UNORM_SRGB:                return SVGA3D_BC7_UNORM_SRGB;
        default:
            AssertFailed();
            break;
    }
    return SVGA3D_FORMAT_INVALID;
}


static int vmsvgaDXCheckFormatSupportPreDX(PVMSVGA3DSTATE pState, SVGA3dSurfaceFormat enmFormat, uint32_t *pu32DevCap)
{
    int rc = VINF_SUCCESS;

    *pu32DevCap = 0;

    DXGI_FORMAT const dxgiFormat = vmsvgaDXSurfaceFormat2Dxgi(enmFormat);
    if (dxgiFormat != DXGI_FORMAT_UNKNOWN)
    {
        RT_NOREF(pState);
        /** @todo Implement */
    }
    else
        rc = VERR_NOT_SUPPORTED;
    return rc;
}


static int vmsvgaDXCheckFormatSupport(PVMSVGA3DSTATE pState, SVGA3dSurfaceFormat enmFormat, uint32_t *pu32DevCap)
{
    int rc = VINF_SUCCESS;

    *pu32DevCap = 0;

    DXGI_FORMAT const dxgiFormat = vmsvgaDXSurfaceFormat2Dxgi(enmFormat);
    if (dxgiFormat != DXGI_FORMAT_UNKNOWN)
    {
        ID3D11Device *pDevice = pState->pBackend->dxDevice.pDevice;
        UINT FormatSupport = 0;
        HRESULT hr = pDevice->CheckFormatSupport(dxgiFormat, &FormatSupport);
        if (SUCCEEDED(hr))
        {
            *pu32DevCap |= SVGA3D_DXFMT_SUPPORTED;

            if (FormatSupport & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE)
                *pu32DevCap |= SVGA3D_DXFMT_SHADER_SAMPLE;

            if (FormatSupport & D3D11_FORMAT_SUPPORT_RENDER_TARGET)
                *pu32DevCap |= SVGA3D_DXFMT_COLOR_RENDERTARGET;

            if (FormatSupport & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL)
                *pu32DevCap |= SVGA3D_DXFMT_DEPTH_RENDERTARGET;

            if (FormatSupport & D3D11_FORMAT_SUPPORT_BLENDABLE)
                *pu32DevCap |= SVGA3D_DXFMT_BLENDABLE;

            if (FormatSupport & D3D11_FORMAT_SUPPORT_MIP)
                *pu32DevCap |= SVGA3D_DXFMT_MIPS;

            if (FormatSupport & D3D11_FORMAT_SUPPORT_TEXTURECUBE)
                *pu32DevCap |= SVGA3D_DXFMT_ARRAY;

            if (FormatSupport & D3D11_FORMAT_SUPPORT_TEXTURE3D)
                *pu32DevCap |= SVGA3D_DXFMT_VOLUME;

            if (FormatSupport & D3D11_FORMAT_SUPPORT_IA_VERTEX_BUFFER)
                *pu32DevCap |= SVGA3D_DXFMT_DX_VERTEX_BUFFER;

            if (pState->pBackend->dxDevice.MultisampleCountMask != 0)
            {
                UINT NumQualityLevels;
                hr = pDevice->CheckMultisampleQualityLevels(dxgiFormat, 2, &NumQualityLevels);
                if (SUCCEEDED(hr) && NumQualityLevels != 0)
                    *pu32DevCap |= SVGA3D_DXFMT_MULTISAMPLE;
            }
        }
        else
        {
            LogFunc(("CheckFormatSupport failed for 0x%08x, hr = 0x%08x\n", dxgiFormat, hr));
            rc = VERR_NOT_SUPPORTED;
        }
    }
    else
        rc = VERR_NOT_SUPPORTED;
    return rc;
}


static void dxLogRelVideoCaps(ID3D11VideoDevice *pVideoDevice)
{
#define FORMAT_ELEMENT(aFormat) { aFormat, #aFormat }
    static const struct {
        DXGI_FORMAT format;
        char const *pszFormat;
    } aVDFormats[] =
    {
        FORMAT_ELEMENT(DXGI_FORMAT_NV12),
        FORMAT_ELEMENT(DXGI_FORMAT_YUY2),
        FORMAT_ELEMENT(DXGI_FORMAT_AYUV),
    };

    static const struct {
        DXGI_FORMAT format;
        char const *pszFormat;
    } aVPFormats[] =
    {
        // Queried from driver
        FORMAT_ELEMENT(DXGI_FORMAT_R8_UNORM),
        FORMAT_ELEMENT(DXGI_FORMAT_R16_UNORM),
        FORMAT_ELEMENT(DXGI_FORMAT_NV12),
        FORMAT_ELEMENT(DXGI_FORMAT_420_OPAQUE),
        FORMAT_ELEMENT(DXGI_FORMAT_P010),
        FORMAT_ELEMENT(DXGI_FORMAT_P016),
        FORMAT_ELEMENT(DXGI_FORMAT_YUY2),
        FORMAT_ELEMENT(DXGI_FORMAT_NV11),
        FORMAT_ELEMENT(DXGI_FORMAT_AYUV),
        FORMAT_ELEMENT(DXGI_FORMAT_R16G16B16A16_FLOAT),
        FORMAT_ELEMENT(DXGI_FORMAT_Y216),
        FORMAT_ELEMENT(DXGI_FORMAT_B8G8R8X8_UNORM),
        FORMAT_ELEMENT(DXGI_FORMAT_B8G8R8A8_UNORM),
        FORMAT_ELEMENT(DXGI_FORMAT_R8G8B8A8_UNORM),
        FORMAT_ELEMENT(DXGI_FORMAT_R10G10B10A2_UNORM),

        // From format table
        FORMAT_ELEMENT(DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM),
        FORMAT_ELEMENT(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB),
        FORMAT_ELEMENT(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB),
        FORMAT_ELEMENT(DXGI_FORMAT_Y410),
        FORMAT_ELEMENT(DXGI_FORMAT_Y416),
        FORMAT_ELEMENT(DXGI_FORMAT_Y210),
        FORMAT_ELEMENT(DXGI_FORMAT_AI44),
        FORMAT_ELEMENT(DXGI_FORMAT_IA44),
        FORMAT_ELEMENT(DXGI_FORMAT_P8),
        FORMAT_ELEMENT(DXGI_FORMAT_A8P8),
    };
#undef FORMAT_ELEMENT

    static char const *apszFilterName[] =
    {
        "BRIGHTNESS",
        "CONTRAST",
        "HUE",
        "SATURATION",
        "NOISE_REDUCTION",
        "EDGE_ENHANCEMENT",
        "ANAMORPHIC_SCALING",
        "STEREO_ADJUSTMENT",
    };

    HRESULT hr;

    UINT cProfiles = pVideoDevice->GetVideoDecoderProfileCount();
    for (UINT i = 0; i < cProfiles; ++i)
    {
        GUID DecodeProfile;
        hr = pVideoDevice->GetVideoDecoderProfile(i, &DecodeProfile);
        if (SUCCEEDED(hr))
            LogRel(("VMSVGA: DecodeProfile[%d]: %RTuuid\n", i, &DecodeProfile));
    }

    D3D11_VIDEO_DECODER_DESC DecoderDesc;
    // Commonly used D3D11_DECODER_PROFILE_H264_VLD_NOFGT
    DecoderDesc.Guid         = { 0x1b81be68, 0xa0c7,0x11d3,{0xb9,0x84,0x00,0xc0,0x4f,0x2e,0x73,0xc5}};
    DecoderDesc.SampleWidth  = 1920;
    DecoderDesc.SampleHeight = 1080;
    DecoderDesc.OutputFormat = DXGI_FORMAT_NV12;

    UINT cConfigs = 0;
    hr = pVideoDevice->GetVideoDecoderConfigCount(&DecoderDesc, &cConfigs);
    for (UINT i = 0; i < cConfigs; ++i)
    {
        D3D11_VIDEO_DECODER_CONFIG DecoderConfig;
        hr = pVideoDevice->GetVideoDecoderConfig(&DecoderDesc, i, &DecoderConfig);
        if (SUCCEEDED(hr))
        {
            LogRel2(("VMSVGA: Config[%d]:\n"
                     "VMSVGA:   %RTuuid\n"
                     "VMSVGA:   %RTuuid\n"
                     "VMSVGA:   %RTuuid\n"
                     "VMSVGA:   BitstreamRaw 0x%x\n"
                     "VMSVGA:   MBCRO        0x%x\n"
                     "VMSVGA:   RDH          0x%x\n"
                     "VMSVGA:   SR8          0x%x\n"
                     "VMSVGA:   R8Sub        0x%x\n"
                     "VMSVGA:   SH8or9C      0x%x\n"
                     "VMSVGA:   SRInterlea   0x%x\n"
                     "VMSVGA:   IRUnsigned   0x%x\n"
                     "VMSVGA:   RDAccel      0x%x\n"
                     "VMSVGA:   HInvScan     0x%x\n"
                     "VMSVGA:   SpecIDCT     0x%x\n"
                     "VMSVGA:   4GCoefs      0x%x\n"
                     "VMSVGA:   MinRTBC      0x%x\n"
                     "VMSVGA:   DecSpec      0x%x\n"
                     ,
                     i, &DecoderConfig.guidConfigBitstreamEncryption,
                     &DecoderConfig.guidConfigMBcontrolEncryption,
                     &DecoderConfig.guidConfigResidDiffEncryption,
                     DecoderConfig.ConfigBitstreamRaw,
                     DecoderConfig.ConfigMBcontrolRasterOrder,
                     DecoderConfig.ConfigResidDiffHost,
                     DecoderConfig.ConfigSpatialResid8,
                     DecoderConfig.ConfigResid8Subtraction,
                     DecoderConfig.ConfigSpatialHost8or9Clipping,
                     DecoderConfig.ConfigSpatialResidInterleaved,
                     DecoderConfig.ConfigIntraResidUnsigned,
                     DecoderConfig.ConfigResidDiffAccelerator,
                     DecoderConfig.ConfigHostInverseScan,
                     DecoderConfig.ConfigSpecificIDCT,
                     DecoderConfig.Config4GroupedCoefs,
                     DecoderConfig.ConfigMinRenderTargetBuffCount,
                     DecoderConfig.ConfigDecoderSpecific
                   ));
        }
    }

    for (UINT idxFormat = 0; idxFormat < RT_ELEMENTS(aVDFormats); ++idxFormat)
    {
        BOOL Supported;
        hr = pVideoDevice->CheckVideoDecoderFormat(&DecoderDesc.Guid, aVDFormats[idxFormat].format, &Supported);
        if (FAILED(hr))
            Supported = FALSE;
        LogRel(("VMSVGA: %s: %s\n", aVDFormats[idxFormat].pszFormat, Supported ? "supported" : "-"));
    }

    /* An arbitrary common video content. */
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC Desc;
    Desc.InputFrameFormat            = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    Desc.InputFrameRate.Numerator    = 25;
    Desc.InputFrameRate.Denominator  = 1;
    Desc.InputWidth                  = 1920;
    Desc.InputHeight                 = 1080;
    Desc.OutputFrameRate.Numerator   = 30;
    Desc.OutputFrameRate.Denominator = 1;
    Desc.OutputWidth                 = 864;
    Desc.OutputHeight                = 486;
    Desc.Usage                       = D3D11_VIDEO_USAGE_OPTIMAL_QUALITY;

    ID3D11VideoProcessorEnumerator *pEnum = 0;
    hr = pVideoDevice->CreateVideoProcessorEnumerator(&Desc, &pEnum);
    if (SUCCEEDED(hr))
    {
        for (unsigned i = 0; i < RT_ELEMENTS(aVPFormats); ++i)
        {
            UINT Flags;
            hr = pEnum->CheckVideoProcessorFormat(aVPFormats[i].format, &Flags);
            if (FAILED(hr))
                Flags = 0;
            LogRel(("VMSVGA: %s: flags %x\n", aVPFormats[i].pszFormat, Flags));
        }

        D3D11_VIDEO_PROCESSOR_CAPS Caps;
        hr = pEnum->GetVideoProcessorCaps(&Caps);
        if (SUCCEEDED(hr))
        {
            LogRel(("VMSVGA: VideoProcessorCaps:\n"
                    "VMSVGA:   DeviceCaps %x\n"
                    "VMSVGA:   FeatureCaps %x\n"
                    "VMSVGA:   FilterCaps %x\n"
                    "VMSVGA:   InputFormatCaps %x\n"
                    "VMSVGA:   AutoStreamCaps %x\n"
                    "VMSVGA:   StereoCaps %x\n"
                    "VMSVGA:   RateConversionCapsCount %d\n"
                    "VMSVGA:   MaxInputStreams %d\n"
                    "VMSVGA:   MaxStreamStates %d\n"
                    "",
                    Caps.DeviceCaps,
                    Caps.FeatureCaps,
                    Caps.FilterCaps,
                    Caps.InputFormatCaps,
                    Caps.AutoStreamCaps,
                    Caps.StereoCaps,
                    Caps.RateConversionCapsCount,
                    Caps.MaxInputStreams,
                    Caps.MaxStreamStates
                  ));

            for (unsigned i = 0; i < RT_ELEMENTS(apszFilterName); ++i)
            {
                if (Caps.FilterCaps & (1 << i))
                {
                    D3D11_VIDEO_PROCESSOR_FILTER_RANGE Range;
                    hr = pEnum->GetVideoProcessorFilterRange((D3D11_VIDEO_PROCESSOR_FILTER)i, &Range);
                    if (SUCCEEDED(hr))
                    {
                        LogRel(("VMSVGA: Filter[%s]: Min %d, Max %d, Default %d, Multiplier " FLOAT_FMT_STR "\n",
                                 apszFilterName[i],
                                 Range.Minimum,
                                 Range.Maximum,
                                 Range.Default,
                                 FLOAT_FMT_ARGS(Range.Multiplier)
                                ));
                    }
                }
            }

            for (unsigned idxRateCaps = 0; idxRateCaps < Caps.RateConversionCapsCount; ++idxRateCaps)
            {
                D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS RateCaps;
                hr = pEnum->GetVideoProcessorRateConversionCaps(idxRateCaps, &RateCaps);
                if (SUCCEEDED(hr))
                {
                    LogRel(("VMSVGA: RateConversionCaps[%u]:\n"
                            "VMSVGA:   PastFrames %d\n"
                            "VMSVGA:   FutureFrames %d\n"
                            "VMSVGA:   ProcessorCaps %x\n"
                            "VMSVGA:   ITelecineCaps %x\n"
                            "VMSVGA:   CustomRateCount %d\n"
                            "",
                            idxRateCaps,
                            RateCaps.PastFrames,
                            RateCaps.FutureFrames,
                            RateCaps.ProcessorCaps,
                            RateCaps.ITelecineCaps,
                            RateCaps.CustomRateCount
                          ));

                    for (unsigned idxCustomRateCap = 0; idxCustomRateCap < RateCaps.CustomRateCount; ++idxCustomRateCap)
                    {
                        D3D11_VIDEO_PROCESSOR_CUSTOM_RATE Rate;
                        hr = pEnum->GetVideoProcessorCustomRate(idxRateCaps, idxCustomRateCap, &Rate);
                        if (SUCCEEDED(hr))
                        {
                            LogRel(("VMSVGA:   CustomRate[%u][%u]:\n"
                                    "VMSVGA:     CustomRate %d/%d\n"
                                    "VMSVGA:     OutputFrames %d\n"
                                    "VMSVGA:     InputInterlaced %d\n"
                                    "VMSVGA:     InputFramesOrFields %d\n"
                                    "",
                                    idxRateCaps, idxCustomRateCap,
                                    Rate.CustomRate.Numerator,
                                    Rate.CustomRate.Denominator,
                                    Rate.OutputFrames,
                                    Rate.InputInterlaced,
                                    Rate.InputFramesOrFields
                                  ));
                        }
                    }
                }
            }
        }

        D3D_RELEASE(pEnum);
    }
}


static int dxDeviceCreate(PVMSVGA3DBACKEND pBackend, DXDEVICE *pDXDevice)
{
    int rc = VINF_SUCCESS;

    IDXGIAdapter *pAdapter = NULL; /* Default adapter. */
    static D3D_FEATURE_LEVEL const s_aFeatureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };
    UINT Flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef DEBUG
    Flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    ID3D11Device *pDevice = 0;
    ID3D11DeviceContext *pImmediateContext = 0;
    HRESULT hr = pBackend->pfnD3D11CreateDevice(pAdapter,
                                                D3D_DRIVER_TYPE_HARDWARE,
                                                NULL,
                                                Flags,
                                                s_aFeatureLevels,
                                                RT_ELEMENTS(s_aFeatureLevels),
                                                D3D11_SDK_VERSION,
                                                &pDevice,
                                                &pDXDevice->FeatureLevel,
                                                &pImmediateContext);
#ifdef DEBUG
    if (FAILED(hr))
    {
        /* Device creation may fail because _DEBUG flag requires "D3D11 SDK Layers for Windows 10" ("Graphics Tools"):
         *   Settings/System/Apps/Optional features/Add a feature/Graphics Tools
         * Retry without the flag.
         */
        Flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = pBackend->pfnD3D11CreateDevice(pAdapter,
                                            D3D_DRIVER_TYPE_HARDWARE,
                                            NULL,
                                            Flags,
                                            s_aFeatureLevels,
                                            RT_ELEMENTS(s_aFeatureLevels),
                                            D3D11_SDK_VERSION,
                                            &pDevice,
                                            &pDXDevice->FeatureLevel,
                                            &pImmediateContext);
    }
#endif

    if (SUCCEEDED(hr))
    {
        LogRel(("VMSVGA: Feature level %#x\n", pDXDevice->FeatureLevel));

        hr = pDevice->QueryInterface(__uuidof(ID3D11Device1), (void**)&pDXDevice->pDevice);
        AssertReturnStmt(SUCCEEDED(hr),
                         D3D_RELEASE(pImmediateContext); D3D_RELEASE(pDevice),
                         VERR_NOT_SUPPORTED);

        hr = pImmediateContext->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&pDXDevice->pImmediateContext);
        AssertReturnStmt(SUCCEEDED(hr),
                         D3D_RELEASE(pImmediateContext); D3D_RELEASE(pDXDevice->pDevice); D3D_RELEASE(pDevice),
                         VERR_NOT_SUPPORTED);

        HRESULT hr2;
#ifdef DEBUG
        /* Break into debugger when DX runtime detects anything unusual. */
        ID3D11Debug *pDebug = 0;
        hr2 = pDXDevice->pDevice->QueryInterface(__uuidof(ID3D11Debug), (void**)&pDebug);
        if (SUCCEEDED(hr2))
        {
            ID3D11InfoQueue *pInfoQueue = 0;
            hr2 = pDebug->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&pInfoQueue);
            if (SUCCEEDED(hr2))
            {
                pInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
//                pInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
//                pInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, true);

                /* No breakpoints for the following messages. */
                D3D11_MESSAGE_ID saIgnoredMessageIds[] =
                {
                    /* Message ID:                                    Caused by: */
                    D3D11_MESSAGE_ID_CREATEINPUTLAYOUT_TYPE_MISMATCH, /* Autogenerated input signatures. */
                    D3D11_MESSAGE_ID_LIVE_DEVICE,                     /* Live object report. Does not seem to prevent a breakpoint. */
                    (D3D11_MESSAGE_ID)3146081 /*DEVICE_DRAW_RENDERTARGETVIEW_NOT_SET*/, /* U. */
                    D3D11_MESSAGE_ID_DEVICE_DRAW_SAMPLER_NOT_SET,     /* U. */
                    D3D11_MESSAGE_ID_DEVICE_DRAW_SAMPLER_MISMATCH,    /* U. */
                    D3D11_MESSAGE_ID_CREATEINPUTLAYOUT_EMPTY_LAYOUT,  /* P. */
                    D3D11_MESSAGE_ID_DEVICE_SHADER_LINKAGE_REGISTERMASK, /* S. */
                };

                D3D11_INFO_QUEUE_FILTER filter;
                RT_ZERO(filter);
                filter.DenyList.NumIDs = RT_ELEMENTS(saIgnoredMessageIds);
                filter.DenyList.pIDList = saIgnoredMessageIds;
                pInfoQueue->AddStorageFilterEntries(&filter);

                D3D_RELEASE(pInfoQueue);
            }
            D3D_RELEASE(pDebug);
        }
#endif

        IDXGIDevice *pDxgiDevice = 0;
        hr = pDXDevice->pDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDxgiDevice);
        if (SUCCEEDED(hr))
        {
            IDXGIAdapter *pDxgiAdapter = 0;
            hr = pDxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void**)&pDxgiAdapter);
            if (SUCCEEDED(hr))
            {
                hr = pDxgiAdapter->GetParent(__uuidof(IDXGIFactory), (void**)&pDXDevice->pDxgiFactory);
                D3D_RELEASE(pDxgiAdapter);
            }

            D3D_RELEASE(pDxgiDevice);
        }

        /* Failure to query VideoDevice should be ignored. */
        hr2 = pDXDevice->pDevice->QueryInterface(__uuidof(ID3D11VideoDevice), (void**)&pDXDevice->pVideoDevice);
        Assert(SUCCEEDED(hr2));
        if (SUCCEEDED(hr2))
        {
            hr2 = pDXDevice->pImmediateContext->QueryInterface(__uuidof(ID3D11VideoContext), (void**)&pDXDevice->pVideoContext);
            Assert(SUCCEEDED(hr2));
            if (SUCCEEDED(hr2))
            {
                LogRel(("VMSVGA: VideoDevice available\n"));
            }
            else
            {
                D3D_RELEASE(pDXDevice->pVideoDevice);
                pDXDevice->pVideoContext = NULL;
            }
        }
        else
            pDXDevice->pVideoDevice = NULL;
    }

    if (SUCCEEDED(hr))
        BlitInit(&pDXDevice->Blitter, pDXDevice->pDevice, pDXDevice->pImmediateContext);
    else
        rc = VERR_NOT_SUPPORTED;

    if (SUCCEEDED(hr))
    {
        /* Query multisample support for a common format. */
        DXGI_FORMAT const dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

        for (uint32_t i = 2; i <= D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT; i *= 2)
        {
            UINT NumQualityLevels = 0;
            HRESULT hr2 = pDXDevice->pDevice->CheckMultisampleQualityLevels(dxgiFormat, i, &NumQualityLevels);
            if (SUCCEEDED(hr2) && NumQualityLevels > 0)
                pDXDevice->MultisampleCountMask |= UINT32_C(1) << (i - 1);
        }
    }
    return rc;
}


static void dxDeviceDestroy(PVMSVGA3DBACKEND pBackend, DXDEVICE *pDevice)
{
    RT_NOREF(pBackend);

    BlitRelease(&pDevice->Blitter);

    D3D_RELEASE(pDevice->pVideoDevice);
    D3D_RELEASE(pDevice->pVideoContext);

    D3D_RELEASE(pDevice->pDxgiFactory);
    D3D_RELEASE(pDevice->pImmediateContext);

#ifdef DEBUG
    HRESULT hr2;
    ID3D11Debug *pDebug = 0;
    hr2 = pDevice->pDevice->QueryInterface(__uuidof(ID3D11Debug), (void**)&pDebug);
    if (SUCCEEDED(hr2))
    {
        /// @todo Use this to see whether all resources have been properly released.
        //DEBUG_BREAKPOINT_TEST();
        //pDebug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | (D3D11_RLDO_FLAGS)0x4 /*D3D11_RLDO_IGNORE_INTERNAL*/);
        D3D_RELEASE(pDebug);
    }
#endif

    D3D_RELEASE(pDevice->pDevice);
    RT_ZERO(*pDevice);
}


static void dxViewAddToList(PVGASTATECC pThisCC, DXVIEW *pDXView)
{
    LogFunc(("cid = %u, sid = %u, viewId = %u, type = %u\n",
             pDXView->cid, pDXView->sid, pDXView->viewId, pDXView->enmViewType));

    Assert(pDXView->u.pView); /* Only already created views should be added. Guard against mis-use by callers. */

    PVMSVGA3DSURFACE pSurface;
    int rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, pDXView->sid, &pSurface);
    AssertRCReturnVoid(rc);

    RTListAppend(&pSurface->pBackendSurface->listView, &pDXView->nodeSurfaceView);
}


static void dxViewRemoveFromList(DXVIEW *pDXView)
{
    LogFunc(("cid = %u, sid = %u, viewId = %u, type = %u\n",
             pDXView->cid, pDXView->sid, pDXView->viewId, pDXView->enmViewType));
    /* pView can be NULL, if COT entry is already empty. */
    if (pDXView->u.pView)
    {
        Assert(pDXView->nodeSurfaceView.pNext && pDXView->nodeSurfaceView.pPrev);
        RTListNodeRemove(&pDXView->nodeSurfaceView);
    }
}


static int dxViewDestroy(DXVIEW *pDXView)
{
    LogFunc(("cid = %u, sid = %u, viewId = %u, type = %u\n",
             pDXView->cid, pDXView->sid, pDXView->viewId, pDXView->enmViewType));
    if (pDXView->u.pView)
    {
        D3D_RELEASE(pDXView->u.pView);
        RTListNodeRemove(&pDXView->nodeSurfaceView);
        RT_ZERO(*pDXView);
    }

    return VINF_SUCCESS;
}


static int dxViewInit(DXVIEW *pDXView, PVMSVGA3DSURFACE pSurface, VMSVGA3DDXCONTEXT *pDXContext, uint32_t viewId, VMSVGA3DBACKVIEWTYPE enmViewType, ID3D11View *pView)
{
    pDXView->cid         = pDXContext->cid;
    pDXView->sid         = pSurface->id;
    pDXView->viewId      = viewId;
    pDXView->enmViewType = enmViewType;
    pDXView->u.pView     = pView;
    RTListAppend(&pSurface->pBackendSurface->listView, &pDXView->nodeSurfaceView);

    LogFunc(("cid = %u, sid = %u, viewId = %u, type = %u\n",
              pDXView->cid, pDXView->sid, pDXView->viewId, pDXView->enmViewType));

DXVIEW *pIter, *pNext;
RTListForEachSafe(&pSurface->pBackendSurface->listView, pIter, pNext, DXVIEW, nodeSurfaceView)
{
    AssertPtr(pNext);
    LogFunc(("pIter=%p, pNext=%p\n", pIter, pNext));
}

    return VINF_SUCCESS;
}


static DXDEVICE *dxDeviceGet(PVMSVGA3DSTATE p3dState)
{
    DXDEVICE *pDXDevice = &p3dState->pBackend->dxDevice;
#ifdef DEBUG
    HRESULT hr = pDXDevice->pDevice->GetDeviceRemovedReason();
    Assert(SUCCEEDED(hr));
#endif
    return pDXDevice;
}


static int dxDeviceFlush(DXDEVICE *pDevice)
{
    /** @todo Should the flush follow the query submission? */
    pDevice->pImmediateContext->Flush();

    ID3D11Query *pQuery = 0;
    D3D11_QUERY_DESC qd;
    RT_ZERO(qd);
    qd.Query = D3D11_QUERY_EVENT;

    HRESULT hr = pDevice->pDevice->CreateQuery(&qd, &pQuery);
    Assert(hr == S_OK); RT_NOREF(hr);
    pDevice->pImmediateContext->End(pQuery);

    BOOL queryData;
    while (pDevice->pImmediateContext->GetData(pQuery, &queryData, sizeof(queryData), 0) != S_OK)
        RTThreadYield();

    D3D_RELEASE(pQuery);

    return VINF_SUCCESS;
}


static ID3D11Resource *dxResource(PVMSVGA3DSURFACE pSurface)
{
    VMSVGA3DBACKENDSURFACE *pBackendSurface = pSurface->pBackendSurface;
    if (!pBackendSurface)
        AssertFailedReturn(NULL);
    return pBackendSurface->u.pResource;
}

// Not used
#if 0
static uint32_t dxGetRenderTargetViewSid(PVMSVGA3DDXCONTEXT pDXContext, uint32_t renderTargetViewId)
{
    ASSERT_GUEST_RETURN(renderTargetViewId < pDXContext->cot.cRTView, SVGA_ID_INVALID);

    SVGACOTableDXRTViewEntry const *pRTViewEntry = &pDXContext->cot.paRTView[renderTargetViewId];
    return pRTViewEntry->sid;
}
#endif

static int dxDefineStreamOutput(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dStreamOutputId soid, SVGACOTableDXStreamOutputEntry const *pEntry, DXSHADER *pDXShader)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    DXSTREAMOUTPUT *pDXStreamOutput = &pDXContext->pBackendDXContext->paStreamOutput[soid];

    /* Make D3D11_SO_DECLARATION_ENTRY array from SVGA3dStreamOutputDeclarationEntry. */
    SVGA3dStreamOutputDeclarationEntry const *paDecls;
    PVMSVGAMOB pMob = NULL;
    if (pEntry->usesMob)
    {
        pMob = vmsvgaR3MobGet(pSvgaR3State, pEntry->mobid);
        ASSERT_GUEST_RETURN(pMob, VERR_INVALID_PARAMETER);

        /* Create a memory pointer for the MOB, which is accessible by host. */
        int rc = vmsvgaR3MobBackingStoreCreate(pSvgaR3State, pMob, vmsvgaR3MobSize(pMob));
        ASSERT_GUEST_RETURN(RT_SUCCESS(rc), rc);

        /* Get pointer to the shader bytecode. This will also verify the offset. */
        paDecls = (SVGA3dStreamOutputDeclarationEntry const *)vmsvgaR3MobBackingStorePtr(pMob, pEntry->offsetInBytes);
        AssertReturnStmt(paDecls, vmsvgaR3MobBackingStoreDelete(pSvgaR3State, pMob), VERR_INTERNAL_ERROR);
    }
    else
        paDecls = &pEntry->decl[0];

    pDXStreamOutput->cDeclarationEntry = pEntry->numOutputStreamEntries;
    for (uint32_t i = 0; i < pDXStreamOutput->cDeclarationEntry; ++i)
    {
        D3D11_SO_DECLARATION_ENTRY *pDst = &pDXStreamOutput->aDeclarationEntry[i];
        SVGA3dStreamOutputDeclarationEntry const *pSrc = &paDecls[i];

        uint32_t const registerMask = pSrc->registerMask & 0xF;
        unsigned const iFirstBit = ASMBitFirstSetU32(registerMask);
        unsigned const iLastBit = ASMBitLastSetU32(registerMask);

        pDst->Stream         = pSrc->stream;
        pDst->SemanticName   = NULL; /* Semantic name and index will be taken from the shader output declaration. */
        pDst->SemanticIndex  = 0;
        pDst->StartComponent = iFirstBit > 0 ? iFirstBit - 1 : 0;
        pDst->ComponentCount = iFirstBit > 0 ? iLastBit - (iFirstBit - 1) : 0;
        pDst->OutputSlot     = pSrc->outputSlot;
    }

    uint32_t MaxSemanticIndex = 0;
    for (uint32_t i = 0; i < pDXStreamOutput->cDeclarationEntry; ++i)
    {
        D3D11_SO_DECLARATION_ENTRY *pDeclarationEntry = &pDXStreamOutput->aDeclarationEntry[i];
        SVGA3dStreamOutputDeclarationEntry const *decl = &paDecls[i];

        /* Find the corresponding register and mask in the GS shader output. */
        int idxFound = -1;
        for (uint32_t iOutputEntry = 0; iOutputEntry < pDXShader->shaderInfo.cOutputSignature; ++iOutputEntry)
        {
            SVGA3dDXSignatureEntry const *pOutputEntry = &pDXShader->shaderInfo.aOutputSignature[iOutputEntry];
            if (   pOutputEntry->registerIndex == decl->registerIndex
                && (decl->registerMask & ~pOutputEntry->mask) == 0) /* SO decl mask is a subset of shader output mask. */
            {
                idxFound = iOutputEntry;
                break;
            }
        }

        if (idxFound >= 0)
        {
            DXShaderAttributeSemantic const *pOutputSemantic = &pDXShader->shaderInfo.aOutputSemantic[idxFound];
            pDeclarationEntry->SemanticName = pOutputSemantic->pcszSemanticName;
            pDeclarationEntry->SemanticIndex = pOutputSemantic->SemanticIndex;
            MaxSemanticIndex = RT_MAX(MaxSemanticIndex, pOutputSemantic->SemanticIndex);
        }
        else
            AssertFailed();
    }

    /* A geometry shader may return components of the same register as different attributes:
     *
     *   Output signature
     *   Name           Index Mask        Register
     *   ATTRIB         2     xy          2
     *   ATTRIB         3       z         2
     *
     * For ATTRIB 3 the stream output declaration expects StartComponent = 0 and ComponentCount = 1
     * (not StartComponent = 2 and ComponentCount = 1):
     *
     *   Stream output declaration
     *   SemanticName   SemanticIndex StartComponent ComponentCount
     *   ATTRIB         2             0              2
     *   ATTRIB         3             0              1
     *
     * Stream output declaration can have multiple entries for the same attribute.
     * In this case StartComponent is the offset within the attribute.
     *
     *   Output signature
     *   Name           Index Mask        Register
     *   ATTRIB         0     xyzw        0
     *
     *   Stream output declaration
     *   SemanticName   SemanticIndex StartComponent ComponentCount
     *   ATTRIB         0             0              1
     *   ATTRIB         0             1              1
     *
     * StartComponent has been computed as the component offset in a register:
     * 'StartComponent = iFirstBit > 0 ? iFirstBit - 1 : 0;'.
     *
     * StartComponent must be the offset in an attribute.
     */
    for (uint32_t SemanticIndex = 0; SemanticIndex <= MaxSemanticIndex; ++SemanticIndex)
    {
        /* Find minimum StartComponent value for this attribute. */
        uint32_t MinStartComponent = UINT32_MAX;
        for (uint32_t i = 0; i < pDXStreamOutput->cDeclarationEntry; ++i)
        {
            D3D11_SO_DECLARATION_ENTRY *pDeclarationEntry = &pDXStreamOutput->aDeclarationEntry[i];
            if (pDeclarationEntry->SemanticIndex == SemanticIndex)
                MinStartComponent = RT_MIN(MinStartComponent, pDeclarationEntry->StartComponent);
        }

        AssertContinue(MinStartComponent != UINT32_MAX);

        /* Adjust the StartComponent to start from 0 for this attribute. */
        for (uint32_t i = 0; i < pDXStreamOutput->cDeclarationEntry; ++i)
        {
            D3D11_SO_DECLARATION_ENTRY *pDeclarationEntry = &pDXStreamOutput->aDeclarationEntry[i];
            if (pDeclarationEntry->SemanticIndex == SemanticIndex)
                pDeclarationEntry->StartComponent -= MinStartComponent;
        }
    }

    if (pMob)
        vmsvgaR3MobBackingStoreDelete(pSvgaR3State, pMob);

    return VINF_SUCCESS;
}

static void dxDestroyStreamOutput(DXSTREAMOUTPUT *pDXStreamOutput)
{
    RT_ZERO(*pDXStreamOutput);
}

static D3D11_BLEND dxBlendFactorAlpha(uint8_t svgaBlend)
{
    /* "Blend options that end in _COLOR are not allowed." but the guest sometimes sends them. */
    switch (svgaBlend)
    {
        case SVGA3D_BLENDOP_ZERO:                return D3D11_BLEND_ZERO;
        case SVGA3D_BLENDOP_ONE:                 return D3D11_BLEND_ONE;
        case SVGA3D_BLENDOP_SRCCOLOR:            return D3D11_BLEND_SRC_ALPHA;
        case SVGA3D_BLENDOP_INVSRCCOLOR:         return D3D11_BLEND_INV_SRC_ALPHA;
        case SVGA3D_BLENDOP_SRCALPHA:            return D3D11_BLEND_SRC_ALPHA;
        case SVGA3D_BLENDOP_INVSRCALPHA:         return D3D11_BLEND_INV_SRC_ALPHA;
        case SVGA3D_BLENDOP_DESTALPHA:           return D3D11_BLEND_DEST_ALPHA;
        case SVGA3D_BLENDOP_INVDESTALPHA:        return D3D11_BLEND_INV_DEST_ALPHA;
        case SVGA3D_BLENDOP_DESTCOLOR:           return D3D11_BLEND_DEST_ALPHA;
        case SVGA3D_BLENDOP_INVDESTCOLOR:        return D3D11_BLEND_INV_DEST_ALPHA;
        case SVGA3D_BLENDOP_SRCALPHASAT:         return D3D11_BLEND_SRC_ALPHA_SAT;
        case SVGA3D_BLENDOP_BLENDFACTOR:         return D3D11_BLEND_BLEND_FACTOR;
        case SVGA3D_BLENDOP_INVBLENDFACTOR:      return D3D11_BLEND_INV_BLEND_FACTOR;
        case SVGA3D_BLENDOP_SRC1COLOR:           return D3D11_BLEND_SRC1_ALPHA;
        case SVGA3D_BLENDOP_INVSRC1COLOR:        return D3D11_BLEND_INV_SRC1_ALPHA;
        case SVGA3D_BLENDOP_SRC1ALPHA:           return D3D11_BLEND_SRC1_ALPHA;
        case SVGA3D_BLENDOP_INVSRC1ALPHA:        return D3D11_BLEND_INV_SRC1_ALPHA;
        case SVGA3D_BLENDOP_BLENDFACTORALPHA:    return D3D11_BLEND_BLEND_FACTOR;
        case SVGA3D_BLENDOP_INVBLENDFACTORALPHA: return D3D11_BLEND_INV_BLEND_FACTOR;
        default:
            break;
    }
    return D3D11_BLEND_ZERO;
}


static D3D11_BLEND dxBlendFactorColor(uint8_t svgaBlend)
{
    switch (svgaBlend)
    {
        case SVGA3D_BLENDOP_ZERO:                return D3D11_BLEND_ZERO;
        case SVGA3D_BLENDOP_ONE:                 return D3D11_BLEND_ONE;
        case SVGA3D_BLENDOP_SRCCOLOR:            return D3D11_BLEND_SRC_COLOR;
        case SVGA3D_BLENDOP_INVSRCCOLOR:         return D3D11_BLEND_INV_SRC_COLOR;
        case SVGA3D_BLENDOP_SRCALPHA:            return D3D11_BLEND_SRC_ALPHA;
        case SVGA3D_BLENDOP_INVSRCALPHA:         return D3D11_BLEND_INV_SRC_ALPHA;
        case SVGA3D_BLENDOP_DESTALPHA:           return D3D11_BLEND_DEST_ALPHA;
        case SVGA3D_BLENDOP_INVDESTALPHA:        return D3D11_BLEND_INV_DEST_ALPHA;
        case SVGA3D_BLENDOP_DESTCOLOR:           return D3D11_BLEND_DEST_COLOR;
        case SVGA3D_BLENDOP_INVDESTCOLOR:        return D3D11_BLEND_INV_DEST_COLOR;
        case SVGA3D_BLENDOP_SRCALPHASAT:         return D3D11_BLEND_SRC_ALPHA_SAT;
        case SVGA3D_BLENDOP_BLENDFACTOR:         return D3D11_BLEND_BLEND_FACTOR;
        case SVGA3D_BLENDOP_INVBLENDFACTOR:      return D3D11_BLEND_INV_BLEND_FACTOR;
        case SVGA3D_BLENDOP_SRC1COLOR:           return D3D11_BLEND_SRC1_COLOR;
        case SVGA3D_BLENDOP_INVSRC1COLOR:        return D3D11_BLEND_INV_SRC1_COLOR;
        case SVGA3D_BLENDOP_SRC1ALPHA:           return D3D11_BLEND_SRC1_ALPHA;
        case SVGA3D_BLENDOP_INVSRC1ALPHA:        return D3D11_BLEND_INV_SRC1_ALPHA;
        case SVGA3D_BLENDOP_BLENDFACTORALPHA:    return D3D11_BLEND_BLEND_FACTOR;
        case SVGA3D_BLENDOP_INVBLENDFACTORALPHA: return D3D11_BLEND_INV_BLEND_FACTOR;
        default:
            break;
    }
    return D3D11_BLEND_ZERO;
}


static D3D11_BLEND_OP dxBlendOp(uint8_t svgaBlendEq)
{
    return (D3D11_BLEND_OP)svgaBlendEq;
}


static D3D11_LOGIC_OP dxLogicOp(uint8_t svgaLogicEq)
{
    return (D3D11_LOGIC_OP)svgaLogicEq;
}


/** @todo AssertCompile for types like D3D11_COMPARISON_FUNC and SVGA3dComparisonFunc */
static HRESULT dxBlendStateCreate(DXDEVICE *pDevice, SVGACOTableDXBlendStateEntry const *pEntry, ID3D11BlendState1 **pp)
{
    D3D11_BLEND_DESC1 BlendDesc;
    BlendDesc.AlphaToCoverageEnable = RT_BOOL(pEntry->alphaToCoverageEnable);
    BlendDesc.IndependentBlendEnable = RT_BOOL(pEntry->independentBlendEnable);
    for (int i = 0; i < SVGA3D_MAX_RENDER_TARGETS; ++i)
    {
        BlendDesc.RenderTarget[i].BlendEnable           = RT_BOOL(pEntry->perRT[i].blendEnable);
        BlendDesc.RenderTarget[i].LogicOpEnable         = RT_BOOL(pEntry->perRT[i].logicOpEnable);
        BlendDesc.RenderTarget[i].SrcBlend              = dxBlendFactorColor(pEntry->perRT[i].srcBlend);
        BlendDesc.RenderTarget[i].DestBlend             = dxBlendFactorColor(pEntry->perRT[i].destBlend);
        BlendDesc.RenderTarget[i].BlendOp               = dxBlendOp         (pEntry->perRT[i].blendOp);
        BlendDesc.RenderTarget[i].SrcBlendAlpha         = dxBlendFactorAlpha(pEntry->perRT[i].srcBlendAlpha);
        BlendDesc.RenderTarget[i].DestBlendAlpha        = dxBlendFactorAlpha(pEntry->perRT[i].destBlendAlpha);
        BlendDesc.RenderTarget[i].BlendOpAlpha          = dxBlendOp         (pEntry->perRT[i].blendOpAlpha);
        BlendDesc.RenderTarget[i].LogicOp               = dxLogicOp         (pEntry->perRT[i].logicOp);
        BlendDesc.RenderTarget[i].RenderTargetWriteMask = pEntry->perRT[i].renderTargetWriteMask;
    }

    HRESULT hr = pDevice->pDevice->CreateBlendState1(&BlendDesc, pp);
    Assert(SUCCEEDED(hr));
    return hr;
}


static HRESULT dxDepthStencilStateCreate(DXDEVICE *pDevice, SVGACOTableDXDepthStencilEntry const *pEntry, ID3D11DepthStencilState **pp)
{
    D3D11_DEPTH_STENCIL_DESC desc;
    desc.DepthEnable                  = pEntry->depthEnable;
    desc.DepthWriteMask               = (D3D11_DEPTH_WRITE_MASK)pEntry->depthWriteMask;
    desc.DepthFunc                    = (D3D11_COMPARISON_FUNC)pEntry->depthFunc;
    desc.StencilEnable                = pEntry->stencilEnable;
    desc.StencilReadMask              = pEntry->stencilReadMask;
    desc.StencilWriteMask             = pEntry->stencilWriteMask;
    desc.FrontFace.StencilFailOp      = (D3D11_STENCIL_OP)pEntry->frontStencilFailOp;
    desc.FrontFace.StencilDepthFailOp = (D3D11_STENCIL_OP)pEntry->frontStencilDepthFailOp;
    desc.FrontFace.StencilPassOp      = (D3D11_STENCIL_OP)pEntry->frontStencilPassOp;
    desc.FrontFace.StencilFunc        = (D3D11_COMPARISON_FUNC)pEntry->frontStencilFunc;
    desc.BackFace.StencilFailOp       = (D3D11_STENCIL_OP)pEntry->backStencilFailOp;
    desc.BackFace.StencilDepthFailOp  = (D3D11_STENCIL_OP)pEntry->backStencilDepthFailOp;
    desc.BackFace.StencilPassOp       = (D3D11_STENCIL_OP)pEntry->backStencilPassOp;
    desc.BackFace.StencilFunc         = (D3D11_COMPARISON_FUNC)pEntry->backStencilFunc;
    /** @todo frontEnable, backEnable */

    HRESULT hr = pDevice->pDevice->CreateDepthStencilState(&desc, pp);
    Assert(SUCCEEDED(hr));
    return hr;
}


static HRESULT dxSamplerStateCreate(DXDEVICE *pDevice, SVGACOTableDXSamplerEntry const *pEntry, ID3D11SamplerState **pp)
{
    D3D11_SAMPLER_DESC desc;
    /* Guest sometimes sends inconsistent (from D3D11 point of view) set of filter flags. */
    if (pEntry->filter & SVGA3D_FILTER_ANISOTROPIC)
        desc.Filter     = (pEntry->filter & SVGA3D_FILTER_COMPARE)
                        ? D3D11_FILTER_COMPARISON_ANISOTROPIC
                        : D3D11_FILTER_ANISOTROPIC;
    else
        desc.Filter     = (D3D11_FILTER)pEntry->filter;
    desc.AddressU       = (D3D11_TEXTURE_ADDRESS_MODE)pEntry->addressU;
    desc.AddressV       = (D3D11_TEXTURE_ADDRESS_MODE)pEntry->addressV;
    desc.AddressW       = (D3D11_TEXTURE_ADDRESS_MODE)pEntry->addressW;
    desc.MipLODBias     = pEntry->mipLODBias;
    desc.MaxAnisotropy  = RT_CLAMP(pEntry->maxAnisotropy, 1, 16); /* "Valid values are between 1 and 16" */
    desc.ComparisonFunc = (D3D11_COMPARISON_FUNC)pEntry->comparisonFunc;
    desc.BorderColor[0] = pEntry->borderColor.value[0];
    desc.BorderColor[1] = pEntry->borderColor.value[1];
    desc.BorderColor[2] = pEntry->borderColor.value[2];
    desc.BorderColor[3] = pEntry->borderColor.value[3];
    desc.MinLOD         = pEntry->minLOD;
    desc.MaxLOD         = pEntry->maxLOD;

    HRESULT hr = pDevice->pDevice->CreateSamplerState(&desc, pp);
    Assert(SUCCEEDED(hr));
    return hr;
}


static D3D11_FILL_MODE dxFillMode(uint8_t svgaFillMode)
{
    if (svgaFillMode == SVGA3D_FILLMODE_POINT)
        return D3D11_FILL_WIREFRAME;
    return (D3D11_FILL_MODE)svgaFillMode;
}


static D3D11_CULL_MODE dxCullMode(uint8_t svgaCullMode)
{
    return (D3D11_CULL_MODE)svgaCullMode;
}


static HRESULT dxRasterizerStateCreate(DXDEVICE *pDevice, SVGACOTableDXRasterizerStateEntry const *pEntry, ID3D11RasterizerState1 **pp)
{
    D3D11_RASTERIZER_DESC1 desc;
    desc.FillMode              = dxFillMode(pEntry->fillMode);
    desc.CullMode              = dxCullMode(pEntry->cullMode);
    desc.FrontCounterClockwise = pEntry->frontCounterClockwise;
    /** @todo provokingVertexLast */
    desc.DepthBias             = pEntry->depthBias;
    desc.DepthBiasClamp        = pEntry->depthBiasClamp;
    desc.SlopeScaledDepthBias  = pEntry->slopeScaledDepthBias;
    desc.DepthClipEnable       = pEntry->depthClipEnable;
    desc.ScissorEnable         = pEntry->scissorEnable;
    desc.MultisampleEnable     = pEntry->multisampleEnable;
    desc.AntialiasedLineEnable = pEntry->antialiasedLineEnable;
    desc.ForcedSampleCount     = pEntry->forcedSampleCount;
    /** @todo lineWidth lineStippleEnable lineStippleFactor lineStipplePattern */

    HRESULT hr = pDevice->pDevice->CreateRasterizerState1(&desc, pp);
    Assert(SUCCEEDED(hr));
    return hr;
}


static HRESULT dxRenderTargetViewCreate(PVGASTATECC pThisCC, SVGACOTableDXRTViewEntry const *pEntry, VMSVGA3DSURFACE *pSurface, ID3D11RenderTargetView **pp)
{
    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);

    ID3D11Resource *pResource = dxResource(pSurface);

    D3D11_RENDER_TARGET_VIEW_DESC desc;
    RT_ZERO(desc);
    desc.Format = vmsvgaDXSurfaceFormat2Dxgi(pEntry->format);
    AssertReturn(desc.Format != DXGI_FORMAT_UNKNOWN || pEntry->format == SVGA3D_BUFFER, E_FAIL);
    switch (pEntry->resourceDimension)
    {
        case SVGA3D_RESOURCE_BUFFER:
            desc.ViewDimension = D3D11_RTV_DIMENSION_BUFFER;
            desc.Buffer.FirstElement = pEntry->desc.buffer.firstElement;
            desc.Buffer.NumElements = pEntry->desc.buffer.numElements;
            break;
        case SVGA3D_RESOURCE_TEXTURE1D:
            if (pSurface->surfaceDesc.numArrayElements <= 1)
            {
                desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE1D;
                desc.Texture1D.MipSlice = pEntry->desc.tex.mipSlice;
            }
            else
            {
                desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE1DARRAY;
                desc.Texture1DArray.MipSlice = pEntry->desc.tex.mipSlice;
                desc.Texture1DArray.FirstArraySlice = pEntry->desc.tex.firstArraySlice;
                desc.Texture1DArray.ArraySize = pEntry->desc.tex.arraySize;
            }
            break;
        case SVGA3D_RESOURCE_TEXTURE2D:
            if (pSurface->surfaceDesc.numArrayElements <= 1)
            {
                desc.ViewDimension = pSurface->surfaceDesc.multisampleCount > 1
                                   ? D3D11_RTV_DIMENSION_TEXTURE2DMS
                                   : D3D11_RTV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MipSlice = pEntry->desc.tex.mipSlice;
            }
            else
            {
                desc.ViewDimension = pSurface->surfaceDesc.multisampleCount > 1
                                   ? D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY
                                   : D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                desc.Texture2DArray.MipSlice = pEntry->desc.tex.mipSlice;
                desc.Texture2DArray.FirstArraySlice = pEntry->desc.tex.firstArraySlice;
                desc.Texture2DArray.ArraySize = pEntry->desc.tex.arraySize;
            }
            break;
        case SVGA3D_RESOURCE_TEXTURE3D:
            desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
            desc.Texture3D.MipSlice = pEntry->desc.tex3D.mipSlice;
            desc.Texture3D.FirstWSlice = pEntry->desc.tex3D.firstW;
            desc.Texture3D.WSize = pEntry->desc.tex3D.wSize;
            break;
        case SVGA3D_RESOURCE_TEXTURECUBE:
            desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            desc.Texture2DArray.MipSlice = pEntry->desc.tex.mipSlice;
            desc.Texture2DArray.FirstArraySlice = 0;
            desc.Texture2DArray.ArraySize = 6;
            break;
        case SVGA3D_RESOURCE_BUFFEREX:
            AssertFailed(); /** @todo test. Probably not applicable to a render target view. */
            desc.ViewDimension = D3D11_RTV_DIMENSION_BUFFER;
            desc.Buffer.FirstElement = pEntry->desc.buffer.firstElement;
            desc.Buffer.NumElements = pEntry->desc.buffer.numElements;
            break;
        default:
            ASSERT_GUEST_FAILED_RETURN(E_INVALIDARG);
    }

    HRESULT hr = pDevice->pDevice->CreateRenderTargetView(pResource, &desc, pp);
    Assert(SUCCEEDED(hr));
    return hr;
}


static HRESULT dxShaderResourceViewCreate(PVGASTATECC pThisCC, SVGACOTableDXSRViewEntry const *pEntry, VMSVGA3DSURFACE *pSurface, ID3D11ShaderResourceView **pp)
{
    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);

    ID3D11Resource *pResource = dxResource(pSurface);

    D3D11_SHADER_RESOURCE_VIEW_DESC desc;
    RT_ZERO(desc);
    desc.Format = vmsvgaDXSurfaceFormat2Dxgi(pEntry->format);
    AssertReturn(desc.Format != DXGI_FORMAT_UNKNOWN || pEntry->format == SVGA3D_BUFFER, E_FAIL);

    switch (pEntry->resourceDimension)
    {
        case SVGA3D_RESOURCE_BUFFER:
            desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            desc.Buffer.FirstElement = pEntry->desc.buffer.firstElement;
            desc.Buffer.NumElements = pEntry->desc.buffer.numElements;
            break;
        case SVGA3D_RESOURCE_TEXTURE1D:
            if (pSurface->surfaceDesc.numArrayElements <= 1)
            {
                desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
                desc.Texture1D.MostDetailedMip = pEntry->desc.tex.mostDetailedMip;
                desc.Texture1D.MipLevels = pEntry->desc.tex.mipLevels;
            }
            else
            {
                desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1DARRAY;
                desc.Texture1DArray.MostDetailedMip = pEntry->desc.tex.mostDetailedMip;
                desc.Texture1DArray.MipLevels = pEntry->desc.tex.mipLevels;
                desc.Texture1DArray.FirstArraySlice = pEntry->desc.tex.firstArraySlice;
                desc.Texture1DArray.ArraySize = pEntry->desc.tex.arraySize;
            }
            break;
        case SVGA3D_RESOURCE_TEXTURE2D:
            if (pSurface->surfaceDesc.numArrayElements <= 1)
            {
                desc.ViewDimension = pSurface->surfaceDesc.multisampleCount > 1
                                   ? D3D11_SRV_DIMENSION_TEXTURE2DMS
                                   : D3D11_SRV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MostDetailedMip = pEntry->desc.tex.mostDetailedMip;
                desc.Texture2D.MipLevels = pEntry->desc.tex.mipLevels;
            }
            else
            {
                desc.ViewDimension = pSurface->surfaceDesc.multisampleCount > 1
                                   ? D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY
                                   : D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
                desc.Texture2DArray.MostDetailedMip = pEntry->desc.tex.mostDetailedMip;
                desc.Texture2DArray.MipLevels = pEntry->desc.tex.mipLevels;
                desc.Texture2DArray.FirstArraySlice = pEntry->desc.tex.firstArraySlice;
                desc.Texture2DArray.ArraySize = pEntry->desc.tex.arraySize;
            }
            break;
        case SVGA3D_RESOURCE_TEXTURE3D:
            desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
            desc.Texture3D.MostDetailedMip = pEntry->desc.tex.mostDetailedMip;
            desc.Texture3D.MipLevels = pEntry->desc.tex.mipLevels;
            break;
        case SVGA3D_RESOURCE_TEXTURECUBE:
            if (pSurface->surfaceDesc.numArrayElements <= 6)
            {
                desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
                desc.TextureCube.MostDetailedMip = pEntry->desc.tex.mostDetailedMip;
                desc.TextureCube.MipLevels = pEntry->desc.tex.mipLevels;
            }
            else
            {
                desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
                desc.TextureCubeArray.MostDetailedMip = pEntry->desc.tex.mostDetailedMip;
                desc.TextureCubeArray.MipLevels = pEntry->desc.tex.mipLevels;
                desc.TextureCubeArray.First2DArrayFace = pEntry->desc.tex.firstArraySlice;
                desc.TextureCubeArray.NumCubes = pEntry->desc.tex.arraySize / 6;
            }
            break;
        case SVGA3D_RESOURCE_BUFFEREX:
            AssertFailed(); /** @todo test. */
            desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
            desc.BufferEx.FirstElement = pEntry->desc.bufferex.firstElement;
            desc.BufferEx.NumElements = pEntry->desc.bufferex.numElements;
            desc.BufferEx.Flags = pEntry->desc.bufferex.flags;
            break;
        default:
            ASSERT_GUEST_FAILED_RETURN(E_INVALIDARG);
    }

    HRESULT hr = pDevice->pDevice->CreateShaderResourceView(pResource, &desc, pp);
    Assert(SUCCEEDED(hr));
    return hr;
}


static HRESULT dxUnorderedAccessViewCreate(PVGASTATECC pThisCC, SVGACOTableDXUAViewEntry const *pEntry, VMSVGA3DSURFACE *pSurface, ID3D11UnorderedAccessView **pp)
{
    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);

    ID3D11Resource *pResource = dxResource(pSurface);

    D3D11_UNORDERED_ACCESS_VIEW_DESC desc;
    RT_ZERO(desc);
    desc.Format = vmsvgaDXSurfaceFormat2Dxgi(pEntry->format);
    AssertReturn(desc.Format != DXGI_FORMAT_UNKNOWN || pEntry->format == SVGA3D_BUFFER, E_FAIL);

    switch (pEntry->resourceDimension)
    {
        case SVGA3D_RESOURCE_BUFFER:
            desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            desc.Buffer.FirstElement = pEntry->desc.buffer.firstElement;
            desc.Buffer.NumElements = pEntry->desc.buffer.numElements;
            desc.Buffer.Flags = pEntry->desc.buffer.flags;
            break;
        case SVGA3D_RESOURCE_TEXTURE1D:
            if (pSurface->surfaceDesc.numArrayElements <= 1)
            {
                desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE1D;
                desc.Texture1D.MipSlice = pEntry->desc.tex.mipSlice;
            }
            else
            {
                desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE1DARRAY;
                desc.Texture1DArray.MipSlice = pEntry->desc.tex.mipSlice;
                desc.Texture1DArray.FirstArraySlice = pEntry->desc.tex.firstArraySlice;
                desc.Texture1DArray.ArraySize = pEntry->desc.tex.arraySize;
            }
            break;
        case SVGA3D_RESOURCE_TEXTURE2D:
            if (pSurface->surfaceDesc.numArrayElements <= 1)
            {
                desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MipSlice = pEntry->desc.tex.mipSlice;
            }
            else
            {
                desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
                desc.Texture2DArray.MipSlice = pEntry->desc.tex.mipSlice;
                desc.Texture2DArray.FirstArraySlice = pEntry->desc.tex.firstArraySlice;
                desc.Texture2DArray.ArraySize = pEntry->desc.tex.arraySize;
            }
            break;
        case SVGA3D_RESOURCE_TEXTURE3D:
            desc.Texture3D.MipSlice = pEntry->desc.tex3D.mipSlice;
            desc.Texture3D.FirstWSlice = pEntry->desc.tex3D.firstW;
            desc.Texture3D.WSize = pEntry->desc.tex3D.wSize;
            break;
        default:
            ASSERT_GUEST_FAILED_RETURN(E_INVALIDARG);
    }

    HRESULT hr = pDevice->pDevice->CreateUnorderedAccessView(pResource, &desc, pp);
    Assert(SUCCEEDED(hr));
    return hr;
}


static HRESULT dxDepthStencilViewCreate(PVGASTATECC pThisCC, SVGACOTableDXDSViewEntry const *pEntry, VMSVGA3DSURFACE *pSurface, ID3D11DepthStencilView **pp)
{
    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);

    ID3D11Resource *pResource = dxResource(pSurface);

    D3D11_DEPTH_STENCIL_VIEW_DESC desc;
    RT_ZERO(desc);
    desc.Format = vmsvgaDXSurfaceFormat2Dxgi(pEntry->format);
    AssertReturn(desc.Format != DXGI_FORMAT_UNKNOWN || pEntry->format == SVGA3D_BUFFER, E_FAIL);
    desc.Flags = pEntry->flags;
    switch (pEntry->resourceDimension)
    {
        case SVGA3D_RESOURCE_TEXTURE1D:
            if (pSurface->surfaceDesc.numArrayElements <= 1)
            {
                desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE1D;
                desc.Texture1D.MipSlice = pEntry->mipSlice;
            }
            else
            {
                desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE1DARRAY;
                desc.Texture1DArray.MipSlice = pEntry->mipSlice;
                desc.Texture1DArray.FirstArraySlice = pEntry->firstArraySlice;
                desc.Texture1DArray.ArraySize = pEntry->arraySize;
            }
            break;
        case SVGA3D_RESOURCE_TEXTURE2D:
            if (pSurface->surfaceDesc.numArrayElements <= 1)
            {
                desc.ViewDimension = pSurface->surfaceDesc.multisampleCount > 1
                                   ? D3D11_DSV_DIMENSION_TEXTURE2DMS
                                   : D3D11_DSV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MipSlice = pEntry->mipSlice;
            }
            else
            {
                desc.ViewDimension = pSurface->surfaceDesc.multisampleCount > 1
                                   ? D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY
                                   : D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
                desc.Texture2DArray.MipSlice = pEntry->mipSlice;
                desc.Texture2DArray.FirstArraySlice = pEntry->firstArraySlice;
                desc.Texture2DArray.ArraySize = pEntry->arraySize;
            }
            break;
        default:
            ASSERT_GUEST_FAILED_RETURN(E_INVALIDARG);
    }

    HRESULT hr = pDevice->pDevice->CreateDepthStencilView(pResource, &desc, pp);
    Assert(SUCCEEDED(hr));
    return hr;
}


static HRESULT dxShaderCreate(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, DXSHADER *pDXShader)
{
    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);

    HRESULT hr = S_OK;

    switch (pDXShader->enmShaderType)
    {
        case SVGA3D_SHADERTYPE_VS:
            hr = pDevice->pDevice->CreateVertexShader(pDXShader->pvDXBC, pDXShader->cbDXBC, NULL, &pDXShader->pVertexShader);
            Assert(SUCCEEDED(hr));
            break;
        case SVGA3D_SHADERTYPE_PS:
            hr = pDevice->pDevice->CreatePixelShader(pDXShader->pvDXBC, pDXShader->cbDXBC, NULL, &pDXShader->pPixelShader);
            Assert(SUCCEEDED(hr));
            break;
        case SVGA3D_SHADERTYPE_GS:
        {
            SVGA3dStreamOutputId const soid = pDXContext->svgaDXContext.streamOut.soid;
            if (soid == SVGA_ID_INVALID)
            {
                hr = pDevice->pDevice->CreateGeometryShader(pDXShader->pvDXBC, pDXShader->cbDXBC, NULL, &pDXShader->pGeometryShader);
                Assert(SUCCEEDED(hr));
            }
            else
            {
                ASSERT_GUEST_RETURN(soid < pDXContext->pBackendDXContext->cStreamOutput, E_INVALIDARG);

                SVGACOTableDXStreamOutputEntry const *pEntry = &pDXContext->cot.paStreamOutput[soid];
                DXSTREAMOUTPUT *pDXStreamOutput = &pDXContext->pBackendDXContext->paStreamOutput[soid];

                hr = pDevice->pDevice->CreateGeometryShaderWithStreamOutput(pDXShader->pvDXBC, pDXShader->cbDXBC,
                    pDXStreamOutput->aDeclarationEntry, pDXStreamOutput->cDeclarationEntry,
                    pEntry->numOutputStreamStrides ? pEntry->streamOutputStrideInBytes : NULL, pEntry->numOutputStreamStrides,
                    pEntry->rasterizedStream,
                    /*pClassLinkage=*/ NULL, &pDXShader->pGeometryShader);
                AssertBreak(SUCCEEDED(hr));

                pDXShader->soid = soid;
            }
            break;
        }
        case SVGA3D_SHADERTYPE_HS:
            hr = pDevice->pDevice->CreateHullShader(pDXShader->pvDXBC, pDXShader->cbDXBC, NULL, &pDXShader->pHullShader);
            Assert(SUCCEEDED(hr));
            break;
        case SVGA3D_SHADERTYPE_DS:
            hr = pDevice->pDevice->CreateDomainShader(pDXShader->pvDXBC, pDXShader->cbDXBC, NULL, &pDXShader->pDomainShader);
            Assert(SUCCEEDED(hr));
            break;
        case SVGA3D_SHADERTYPE_CS:
            hr = pDevice->pDevice->CreateComputeShader(pDXShader->pvDXBC, pDXShader->cbDXBC, NULL, &pDXShader->pComputeShader);
            Assert(SUCCEEDED(hr));
            break;
        default:
            ASSERT_GUEST_FAILED_RETURN(E_INVALIDARG);
    }

    return hr;
}


static void dxShaderSet(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dShaderType type, DXSHADER *pDXShader)
{
    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);

    switch (type)
    {
        case SVGA3D_SHADERTYPE_VS:
            pDevice->pImmediateContext->VSSetShader(pDXShader ? pDXShader->pVertexShader : NULL, NULL, 0);
            break;
        case SVGA3D_SHADERTYPE_PS:
            pDevice->pImmediateContext->PSSetShader(pDXShader ? pDXShader->pPixelShader : NULL, NULL, 0);
            break;
        case SVGA3D_SHADERTYPE_GS:
        {
            Assert(!pDXShader || (pDXShader->soid == pDXContext->svgaDXContext.streamOut.soid));
            RT_NOREF(pDXContext);
            pDevice->pImmediateContext->GSSetShader(pDXShader ? pDXShader->pGeometryShader : NULL, NULL, 0);
        } break;
        case SVGA3D_SHADERTYPE_HS:
            pDevice->pImmediateContext->HSSetShader(pDXShader ? pDXShader->pHullShader : NULL, NULL, 0);
            break;
        case SVGA3D_SHADERTYPE_DS:
            pDevice->pImmediateContext->DSSetShader(pDXShader ? pDXShader->pDomainShader : NULL, NULL, 0);
            break;
        case SVGA3D_SHADERTYPE_CS:
            pDevice->pImmediateContext->CSSetShader(pDXShader ? pDXShader->pComputeShader : NULL, NULL, 0);
            break;
        default:
            ASSERT_GUEST_FAILED_RETURN_VOID();
    }
}


static void dxConstantBufferSet(DXDEVICE *pDevice, uint32_t slot, SVGA3dShaderType type, ID3D11Buffer *pConstantBuffer)
{
    switch (type)
    {
        case SVGA3D_SHADERTYPE_VS:
            pDevice->pImmediateContext->VSSetConstantBuffers(slot, 1, &pConstantBuffer);
            break;
        case SVGA3D_SHADERTYPE_PS:
            pDevice->pImmediateContext->PSSetConstantBuffers(slot, 1, &pConstantBuffer);
            break;
        case SVGA3D_SHADERTYPE_GS:
            pDevice->pImmediateContext->GSSetConstantBuffers(slot, 1, &pConstantBuffer);
            break;
        case SVGA3D_SHADERTYPE_HS:
            pDevice->pImmediateContext->HSSetConstantBuffers(slot, 1, &pConstantBuffer);
            break;
        case SVGA3D_SHADERTYPE_DS:
            pDevice->pImmediateContext->DSSetConstantBuffers(slot, 1, &pConstantBuffer);
            break;
        case SVGA3D_SHADERTYPE_CS:
            pDevice->pImmediateContext->CSSetConstantBuffers(slot, 1, &pConstantBuffer);
            break;
        default:
            ASSERT_GUEST_FAILED_RETURN_VOID();
    }
}


static void dxSamplerSet(DXDEVICE *pDevice, SVGA3dShaderType type, uint32_t startSampler, uint32_t cSampler, ID3D11SamplerState * const *papSampler)
{
    switch (type)
    {
        case SVGA3D_SHADERTYPE_VS:
            pDevice->pImmediateContext->VSSetSamplers(startSampler, cSampler, papSampler);
            break;
        case SVGA3D_SHADERTYPE_PS:
            pDevice->pImmediateContext->PSSetSamplers(startSampler, cSampler, papSampler);
            break;
        case SVGA3D_SHADERTYPE_GS:
            pDevice->pImmediateContext->GSSetSamplers(startSampler, cSampler, papSampler);
            break;
        case SVGA3D_SHADERTYPE_HS:
            pDevice->pImmediateContext->HSSetSamplers(startSampler, cSampler, papSampler);
            break;
        case SVGA3D_SHADERTYPE_DS:
            pDevice->pImmediateContext->DSSetSamplers(startSampler, cSampler, papSampler);
            break;
        case SVGA3D_SHADERTYPE_CS:
            pDevice->pImmediateContext->CSSetSamplers(startSampler, cSampler, papSampler);
            break;
        default:
            ASSERT_GUEST_FAILED_RETURN_VOID();
    }
}


static void dxShaderResourceViewSet(DXDEVICE *pDevice, SVGA3dShaderType type, uint32_t startView, uint32_t cShaderResourceView, ID3D11ShaderResourceView * const *papShaderResourceView)
{
    switch (type)
    {
        case SVGA3D_SHADERTYPE_VS:
            pDevice->pImmediateContext->VSSetShaderResources(startView, cShaderResourceView, papShaderResourceView);
            break;
        case SVGA3D_SHADERTYPE_PS:
            pDevice->pImmediateContext->PSSetShaderResources(startView, cShaderResourceView, papShaderResourceView);
            break;
        case SVGA3D_SHADERTYPE_GS:
            pDevice->pImmediateContext->GSSetShaderResources(startView, cShaderResourceView, papShaderResourceView);
            break;
        case SVGA3D_SHADERTYPE_HS:
            pDevice->pImmediateContext->HSSetShaderResources(startView, cShaderResourceView, papShaderResourceView);
            break;
        case SVGA3D_SHADERTYPE_DS:
            pDevice->pImmediateContext->DSSetShaderResources(startView, cShaderResourceView, papShaderResourceView);
            break;
        case SVGA3D_SHADERTYPE_CS:
            pDevice->pImmediateContext->CSSetShaderResources(startView, cShaderResourceView, papShaderResourceView);
            break;
        default:
            ASSERT_GUEST_FAILED_RETURN_VOID();
    }
}


static void dxCSUnorderedAccessViewSet(DXDEVICE *pDevice, uint32_t startView, uint32_t cView, ID3D11UnorderedAccessView * const *papUnorderedAccessView, UINT *pUAVInitialCounts)
{
    pDevice->pImmediateContext->CSSetUnorderedAccessViews(startView, cView, papUnorderedAccessView, pUAVInitialCounts);
}


static int dxBackendSurfaceAlloc(PVMSVGA3DBACKENDSURFACE *ppBackendSurface)
{
    PVMSVGA3DBACKENDSURFACE pBackendSurface = (PVMSVGA3DBACKENDSURFACE)RTMemAllocZ(sizeof(VMSVGA3DBACKENDSURFACE));
    AssertPtrReturn(pBackendSurface, VERR_NO_MEMORY);
    RTListInit(&pBackendSurface->listView);
    *ppBackendSurface = pBackendSurface;
    return VINF_SUCCESS;
}


static UINT dxBindFlags(SVGA3dSurfaceAllFlags surfaceFlags)
{
    /* Catch unimplemented flags. */
    Assert(!RT_BOOL(surfaceFlags & (SVGA3D_SURFACE_BIND_LOGICOPS | SVGA3D_SURFACE_BIND_RAW_VIEWS)));

    UINT BindFlags = 0;

    if (surfaceFlags & (SVGA3D_SURFACE_BIND_VERTEX_BUFFER | SVGA3D_SURFACE_HINT_VERTEXBUFFER))
                                                            BindFlags |= D3D11_BIND_VERTEX_BUFFER;
    if (surfaceFlags & (SVGA3D_SURFACE_BIND_INDEX_BUFFER | SVGA3D_SURFACE_HINT_INDEXBUFFER))
                                                            BindFlags |= D3D11_BIND_INDEX_BUFFER;
    if (surfaceFlags & SVGA3D_SURFACE_BIND_CONSTANT_BUFFER) BindFlags |= D3D11_BIND_CONSTANT_BUFFER;
    if (surfaceFlags & SVGA3D_SURFACE_BIND_SHADER_RESOURCE) BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    if (surfaceFlags & SVGA3D_SURFACE_BIND_RENDER_TARGET)   BindFlags |= D3D11_BIND_RENDER_TARGET;
    if (surfaceFlags & SVGA3D_SURFACE_BIND_DEPTH_STENCIL)   BindFlags |= D3D11_BIND_DEPTH_STENCIL;
    if (surfaceFlags & SVGA3D_SURFACE_BIND_STREAM_OUTPUT)   BindFlags |= D3D11_BIND_STREAM_OUTPUT;
    if (surfaceFlags & SVGA3D_SURFACE_BIND_UAVIEW)          BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    if (surfaceFlags & SVGA3D_SURFACE_RESERVED1)            BindFlags |= D3D11_BIND_DECODER;

    return BindFlags;
}


static DXGI_FORMAT dxGetDxgiTypelessFormat(DXGI_FORMAT dxgiFormat)
{
    switch (dxgiFormat)
    {
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
        case DXGI_FORMAT_R32G32B32A32_UINT:
        case DXGI_FORMAT_R32G32B32A32_SINT:
            return DXGI_FORMAT_R32G32B32A32_TYPELESS;       /* 1 */
        case DXGI_FORMAT_R32G32B32_FLOAT:
        case DXGI_FORMAT_R32G32B32_UINT:
        case DXGI_FORMAT_R32G32B32_SINT:
            return DXGI_FORMAT_R32G32B32_TYPELESS;          /* 5 */
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_UNORM:
        case DXGI_FORMAT_R16G16B16A16_UINT:
        case DXGI_FORMAT_R16G16B16A16_SNORM:
        case DXGI_FORMAT_R16G16B16A16_SINT:
            return DXGI_FORMAT_R16G16B16A16_TYPELESS;       /* 9 */
        case DXGI_FORMAT_R32G32_FLOAT:
        case DXGI_FORMAT_R32G32_UINT:
        case DXGI_FORMAT_R32G32_SINT:
            return DXGI_FORMAT_R32G32_TYPELESS;             /* 15 */
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
            return DXGI_FORMAT_R32G8X24_TYPELESS;           /* 19 */
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R10G10B10A2_UINT:
            return DXGI_FORMAT_R10G10B10A2_TYPELESS;        /* 23 */
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_R8G8B8A8_UINT:
        case DXGI_FORMAT_R8G8B8A8_SNORM:
        case DXGI_FORMAT_R8G8B8A8_SINT:
            return DXGI_FORMAT_R8G8B8A8_TYPELESS;           /* 27 */
        case DXGI_FORMAT_R16G16_FLOAT:
        case DXGI_FORMAT_R16G16_UNORM:
        case DXGI_FORMAT_R16G16_UINT:
        case DXGI_FORMAT_R16G16_SNORM:
        case DXGI_FORMAT_R16G16_SINT:
            return DXGI_FORMAT_R16G16_TYPELESS;             /* 33 */
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_R32_FLOAT:
        case DXGI_FORMAT_R32_UINT:
        case DXGI_FORMAT_R32_SINT:
            return DXGI_FORMAT_R32_TYPELESS;                /* 39 */
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
            return DXGI_FORMAT_R24G8_TYPELESS;              /* 44 */
        case DXGI_FORMAT_R8G8_UNORM:
        case DXGI_FORMAT_R8G8_UINT:
        case DXGI_FORMAT_R8G8_SNORM:
        case DXGI_FORMAT_R8G8_SINT:
            return DXGI_FORMAT_R8G8_TYPELESS;               /* 48*/
        case DXGI_FORMAT_R16_FLOAT:
        case DXGI_FORMAT_D16_UNORM:
        case DXGI_FORMAT_R16_UNORM:
        case DXGI_FORMAT_R16_UINT:
        case DXGI_FORMAT_R16_SNORM:
        case DXGI_FORMAT_R16_SINT:
            return DXGI_FORMAT_R16_TYPELESS;                /* 53 */
        case DXGI_FORMAT_R8_UNORM:
        case DXGI_FORMAT_R8_UINT:
        case DXGI_FORMAT_R8_SNORM:
        case DXGI_FORMAT_R8_SINT:
            return DXGI_FORMAT_R8_TYPELESS;                 /* 60*/
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
            return DXGI_FORMAT_BC1_TYPELESS;                /* 70 */
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
            return DXGI_FORMAT_BC2_TYPELESS;                /* 73 */
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
            return DXGI_FORMAT_BC3_TYPELESS;                /* 76 */
        case DXGI_FORMAT_BC4_UNORM:
        case DXGI_FORMAT_BC4_SNORM:
            return DXGI_FORMAT_BC4_TYPELESS;                /* 79 */
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC5_SNORM:
            return DXGI_FORMAT_BC5_TYPELESS;                /* 82 */
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            return DXGI_FORMAT_B8G8R8A8_TYPELESS;           /* 90 */
        case DXGI_FORMAT_B8G8R8X8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
            return DXGI_FORMAT_B8G8R8X8_TYPELESS;           /* 92 */
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
            return DXGI_FORMAT_BC6H_TYPELESS;               /* 94 */
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return DXGI_FORMAT_BC7_TYPELESS;                /* 97 */
        default:
            break;
    }

    return dxgiFormat;
}


static bool dxIsDepthStencilFormat(DXGI_FORMAT dxgiFormat)
{
    switch (dxgiFormat)
    {
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_D16_UNORM:
            return true;
        default:
            break;
    }

    return false;
}


/** @todo Use vmsvga3dBackSurfaceCreateResource instead */
static int vmsvga3dBackSurfaceCreateSoBuffer(PVGASTATECC pThisCC, PVMSVGA3DSURFACE pSurface)
{
    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    /* Buffers should be created as such. */
    AssertReturn(RT_BOOL(pSurface->f.surfaceFlags & SVGA3D_SURFACE_BIND_STREAM_OUTPUT), VERR_INVALID_PARAMETER);

    if (pSurface->pBackendSurface != NULL)
    {
        AssertFailed(); /** @todo Should the function not be used like that? */
        vmsvga3dBackSurfaceDestroy(pThisCC, false, pSurface);
    }

    PVMSVGA3DBACKENDSURFACE pBackendSurface;
    int rc = dxBackendSurfaceAlloc(&pBackendSurface);
    AssertRCReturn(rc, rc);

    D3D11_BUFFER_DESC bd;
    RT_ZERO(bd);
    bd.ByteWidth           = pSurface->paMipmapLevels[0].cbSurface;
    bd.Usage               = D3D11_USAGE_DEFAULT;
    bd.BindFlags           = dxBindFlags(pSurface->f.surfaceFlags);
    bd.CPUAccessFlags      = 0; /// @todo ? D3D11_CPU_ACCESS_READ;
    bd.MiscFlags           = 0;
    bd.StructureByteStride = 0;

    HRESULT hr = pDevice->pDevice->CreateBuffer(&bd, 0, &pBackendSurface->u.pBuffer);
    if (SUCCEEDED(hr))
    {
        /* Map-able Buffer. */
        bd.Usage          = D3D11_USAGE_DYNAMIC;
        bd.BindFlags      = D3D11_BIND_SHADER_RESOURCE; /* Have to specify a supported flag, otherwise E_INVALIDARG will be returned. */
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = pDevice->pDevice->CreateBuffer(&bd, 0, &pBackendSurface->dynamic.pBuffer);
        Assert(SUCCEEDED(hr));
    }

    if (SUCCEEDED(hr))
    {
        /* Staging texture. */
        bd.Usage          = D3D11_USAGE_STAGING;
        bd.BindFlags      = 0; /* No flags allowed. */
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        hr = pDevice->pDevice->CreateBuffer(&bd, 0, &pBackendSurface->staging.pBuffer);
        Assert(SUCCEEDED(hr));
    }

    if (SUCCEEDED(hr))
    {
        /*
         * Success.
         */
        pBackendSurface->enmResType = VMSVGA3D_RESTYPE_BUFFER;
        pBackendSurface->enmDxgiFormat = DXGI_FORMAT_UNKNOWN;
        pSurface->pBackendSurface = pBackendSurface;
        return VINF_SUCCESS;
    }

    /* Failure. */
    D3D_RELEASE(pBackendSurface->u.pBuffer);
    D3D_RELEASE(pBackendSurface->dynamic.pBuffer);
    D3D_RELEASE(pBackendSurface->staging.pBuffer);
    RTMemFree(pBackendSurface);
    return VERR_NO_MEMORY;
}


static HRESULT dxCreateTexture(DXDEVICE *pDXDevice, PVMSVGA3DSURFACE pSurface, bool fIntermediate,
                               D3D11_SUBRESOURCE_DATA *paInitialData,
                               DXGI_FORMAT Format, D3D11_USAGE Usage,
                               UINT BindFlags, UINT CPUAccessFlags, UINT MiscFlags,
                               ID3D11Resource **ppResource, VMSVGA3DBACKRESTYPE *pResourceType)
{
    HRESULT hr;

    uint32_t const cWidth = pSurface->paMipmapLevels[0].cBlocksX * pSurface->cxBlock;
    uint32_t const cHeight = pSurface->paMipmapLevels[0].cBlocksY * pSurface->cyBlock;
    uint32_t const cDepth = pSurface->paMipmapLevels[0].mipmapSize.depth;
    uint32_t const numMipLevels = fIntermediate ? 1 : pSurface->cLevels;
    uint32_t const numArrayElements = fIntermediate ? 1 : pSurface->surfaceDesc.numArrayElements;
    uint32_t const multisampleCount = fIntermediate ? 1 : pSurface->surfaceDesc.multisampleCount;

    if (pSurface->f.surfaceFlags & SVGA3D_SURFACE_CUBEMAP)
    {
        /*
         * Cubemap.
         */
        Assert(pSurface->cFaces == 6);
        Assert(cWidth == cHeight);
        Assert(cDepth == 1);

        D3D11_TEXTURE2D_DESC td;
        RT_ZERO(td);
        td.Width              = cWidth;
        td.Height             = cHeight;
        td.MipLevels          = numMipLevels;
        td.ArraySize          = numArrayElements; /* This is 6 * numCubes */
        td.Format             = Format;
        td.SampleDesc.Count   = multisampleCount;
        td.SampleDesc.Quality = 0;
        td.Usage              = Usage;
        td.BindFlags          = BindFlags;
        td.CPUAccessFlags     = CPUAccessFlags;
        td.MiscFlags          = MiscFlags;
        if (   numMipLevels > 1
            && (td.BindFlags & (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET)) == (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET)
            && !fIntermediate)
            td.MiscFlags     |= D3D11_RESOURCE_MISC_GENERATE_MIPS; /* Required for GenMips. */

        hr = pDXDevice->pDevice->CreateTexture2D(&td, paInitialData, (ID3D11Texture2D **)ppResource);
        Assert(SUCCEEDED(hr));
        if (SUCCEEDED(hr))
            *pResourceType = VMSVGA3D_RESTYPE_TEXTURE_CUBE;
    }
    else if (pSurface->f.surfaceFlags & SVGA3D_SURFACE_1D)
    {
        /*
         * 1D texture.
         */
        Assert(pSurface->cFaces == 1);

        D3D11_TEXTURE1D_DESC td;
        RT_ZERO(td);
        td.Width              = cWidth;
        td.MipLevels          = numMipLevels;
        td.ArraySize          = numArrayElements;
        td.Format             = Format;
        td.Usage              = Usage;
        td.BindFlags          = BindFlags;
        td.CPUAccessFlags     = CPUAccessFlags;
        td.MiscFlags          = MiscFlags;
        if (   numMipLevels > 1
            && (td.BindFlags & (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET)) == (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET)
            && !fIntermediate)
            td.MiscFlags     |= D3D11_RESOURCE_MISC_GENERATE_MIPS; /* Required for GenMips. */

        hr = pDXDevice->pDevice->CreateTexture1D(&td, paInitialData, (ID3D11Texture1D **)ppResource);
        Assert(SUCCEEDED(hr));
        if (SUCCEEDED(hr))
            *pResourceType = VMSVGA3D_RESTYPE_TEXTURE_1D;
    }
    else if (pSurface->f.surfaceFlags & SVGA3D_SURFACE_VOLUME)
    {
        /*
         * Volume texture.
         */
        Assert(pSurface->cFaces == 1);
        Assert(pSurface->surfaceDesc.numArrayElements == 1);

        D3D11_TEXTURE3D_DESC td;
        RT_ZERO(td);
        td.Width              = cWidth;
        td.Height             = cHeight;
        td.Depth              = cDepth;
        td.MipLevels          = numMipLevels;
        td.Format             = Format;
        td.Usage              = Usage;
        td.BindFlags          = BindFlags;
        td.CPUAccessFlags     = CPUAccessFlags;
        td.MiscFlags          = MiscFlags;
        if (   numMipLevels > 1
            && (td.BindFlags & (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET)) == (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET)
            && !fIntermediate)
            td.MiscFlags     |= D3D11_RESOURCE_MISC_GENERATE_MIPS; /* Required for GenMips. */

        hr = pDXDevice->pDevice->CreateTexture3D(&td, paInitialData, (ID3D11Texture3D **)ppResource);
        Assert(SUCCEEDED(hr));
        if (SUCCEEDED(hr))
            *pResourceType = VMSVGA3D_RESTYPE_TEXTURE_3D;
    }
    else
    {
        /*
         * 2D texture.
         */
        Assert(cDepth == 1);
        Assert(pSurface->cFaces == 1);

        D3D11_TEXTURE2D_DESC td;
        RT_ZERO(td);
        td.Width              = cWidth;
        td.Height             = cHeight;
        td.MipLevels          = numMipLevels;
        td.ArraySize          = numArrayElements;
        td.Format             = Format;
        td.SampleDesc.Count   = multisampleCount;
        td.SampleDesc.Quality = 0;
        td.Usage              = Usage;
        td.BindFlags          = BindFlags;
        td.CPUAccessFlags     = CPUAccessFlags;
        td.MiscFlags          = MiscFlags;
        if (   numMipLevels > 1
            && (td.BindFlags & (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET)) == (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET)
            && !fIntermediate)
            td.MiscFlags     |= D3D11_RESOURCE_MISC_GENERATE_MIPS; /* Required for GenMips. */

        hr = pDXDevice->pDevice->CreateTexture2D(&td, paInitialData, (ID3D11Texture2D **)ppResource);
        Assert(SUCCEEDED(hr));
        if (SUCCEEDED(hr))
            *pResourceType = VMSVGA3D_RESTYPE_TEXTURE_2D;
    }

    return hr;
}


static int vmsvga3dBackSurfaceCreateResource(PVGASTATECC pThisCC, PVMSVGA3DSURFACE pSurface)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pDevice, VERR_INVALID_STATE);

    if (pSurface->pBackendSurface != NULL)
    {
        AssertFailed(); /** @todo Should the function not be used like that? */
        vmsvga3dBackSurfaceDestroy(pThisCC, false, pSurface);
    }

    PVMSVGA3DMIPMAPLEVEL pMipLevel;
    int rc = vmsvga3dMipmapLevel(pSurface, 0, 0, &pMipLevel);
    AssertRCReturn(rc, rc);

    PVMSVGA3DBACKENDSURFACE pBackendSurface;
    rc = dxBackendSurfaceAlloc(&pBackendSurface);
    AssertRCReturn(rc, rc);

    HRESULT hr;

    /* Translate SVGA surface flags to D3D11 flags. */
    D3D11_USAGE Usage;
    /* First try to derive the usage from explicit hints. */
    if (pSurface->f.surfaceFlags & (SVGA3D_SURFACE_STAGING_UPLOAD | SVGA3D_SURFACE_STAGING_DOWNLOAD))
        Usage = D3D11_USAGE_STAGING;
    else if (pSurface->f.surfaceFlags & SVGA3D_SURFACE_HINT_DYNAMIC)
        Usage = D3D11_USAGE_DYNAMIC;
    else if (pSurface->f.surfaceFlags & SVGA3D_SURFACE_HINT_STATIC)
    {
        /* Use D3D11_USAGE_DEFAULT instead of D3D11_USAGE_IMMUTABLE to let the guest the guest update
         * the buffer later.
         *
         * The guest issues SVGA_3D_CMD_INVALIDATE_GB_IMAGE followed by SVGA_3D_CMD_UPDATE_GB_IMAGE
         * when the data in SVGA3D_SURFACE_HINT_STATIC surface is updated.
         * D3D11_USAGE_IMMUTABLE would work if the device destroys the D3D buffer on INVALIDATE
         * and re-creates it in setupPipeline with initial data from the backing guest MOB.
         * Currently the device does not destroy the buffer on INVALIDATE. So just use D3D11_USAGE_DEFAULT.
         */
        Usage = D3D11_USAGE_DEFAULT;
    }
    else if (pSurface->f.surfaceFlags & SVGA3D_SURFACE_HINT_INDIRECT_UPDATE)
        Usage = D3D11_USAGE_DEFAULT;
    /* Try to infer the usage from surface flags. */
    else if (   pSurface->f.surfaceFlags & SVGA3D_SURFACE_HINT_INDEXBUFFER
             || pSurface->f.surfaceFlags & SVGA3D_SURFACE_HINT_VERTEXBUFFER
             || pSurface->f.surfaceFlags & SVGA3D_SURFACE_BIND_VERTEX_BUFFER
             || pSurface->f.surfaceFlags & SVGA3D_SURFACE_BIND_INDEX_BUFFER
             || pSurface->f.surfaceFlags & SVGA3D_SURFACE_BIND_CONSTANT_BUFFER)
        Usage = D3D11_USAGE_DYNAMIC;
    else if (   pSurface->f.surfaceFlags & SVGA3D_SURFACE_SCREENTARGET
             || pSurface->f.surfaceFlags & SVGA3D_SURFACE_BIND_SHADER_RESOURCE
             || pSurface->f.surfaceFlags & SVGA3D_SURFACE_BIND_RENDER_TARGET
             || pSurface->f.surfaceFlags & SVGA3D_SURFACE_BIND_DEPTH_STENCIL
             || pSurface->f.surfaceFlags & SVGA3D_SURFACE_BIND_STREAM_OUTPUT)
        Usage = D3D11_USAGE_DEFAULT;
    else
    {
        /// @todo No recognizable hint from guest, figure out what to do.
        ASSERT_GUEST_FAILED();
        Usage = D3D11_USAGE_DEFAULT;
    }

    /// @todo Bind flags here instead of a separate function
    UINT const BindFlags = dxBindFlags(pSurface->f.surfaceFlags);

    UINT CPUAccessFlags = 0;
    if (Usage == D3D11_USAGE_STAGING)
        CPUAccessFlags |= D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ;
    else if (Usage == D3D11_USAGE_DYNAMIC)
        CPUAccessFlags |= D3D11_CPU_ACCESS_WRITE;

    UINT MiscFlags = 0;
    if (pSurface->f.surfaceFlags & SVGA3D_SURFACE_CUBEMAP)
        MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
    if (pSurface->f.surfaceFlags & SVGA3D_SURFACE_DRAWINDIRECT_ARGS)
        MiscFlags |= D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
    if (pSurface->f.surfaceFlags & SVGA3D_SURFACE_BIND_RAW_VIEWS)
        MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    if (pSurface->f.surfaceFlags & SVGA3D_SURFACE_BUFFER_STRUCTURED)
        MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    if (pSurface->f.surfaceFlags & SVGA3D_SURFACE_RESOURCE_CLAMP)
        MiscFlags |= D3D11_RESOURCE_MISC_RESOURCE_CLAMP;

    /*
     * Figure out the type of the surface.
     */
    if (pSurface->format == SVGA3D_BUFFER)
    {
        /* Upload the current data, if any. */
        D3D11_SUBRESOURCE_DATA *pInitialData = NULL;
        D3D11_SUBRESOURCE_DATA initialData;
        if (pMipLevel->pSurfaceData)
        {
            initialData.pSysMem          = pMipLevel->pSurfaceData;
            initialData.SysMemPitch      = pMipLevel->cbSurface;
            initialData.SysMemSlicePitch = pMipLevel->cbSurface;

            pInitialData = &initialData;
        }

        D3D11_BUFFER_DESC bd;
        RT_ZERO(bd);
        bd.ByteWidth = pMipLevel->cbSurface;
        bd.Usage = Usage;
        bd.BindFlags = BindFlags;
        bd.CPUAccessFlags = CPUAccessFlags;
        bd.MiscFlags = MiscFlags;

        if (bd.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED)
        {
            SVGAOTableSurfaceEntry entrySurface;
            rc = vmsvgaR3OTableReadSurface(pThisCC->svga.pSvgaR3State, pSurface->id, &entrySurface);
            AssertRCReturn(rc, rc);

            bd.StructureByteStride = entrySurface.bufferByteStride;
        }

        hr = pDXDevice->pDevice->CreateBuffer(&bd, pInitialData, &pBackendSurface->u.pBuffer);
        Assert(SUCCEEDED(hr));
        if (SUCCEEDED(hr))
        {
            pBackendSurface->enmResType = VMSVGA3D_RESTYPE_BUFFER;
            pBackendSurface->enmDxgiFormat = DXGI_FORMAT_UNKNOWN;
            pBackendSurface->enmD3D11Usage = Usage;
        }
    }
    else
    {
        DXGI_FORMAT dxgiFormat = vmsvgaDXSurfaceFormat2Dxgi(pSurface->format);
        AssertReturn(dxgiFormat != DXGI_FORMAT_UNKNOWN, E_FAIL);

        /* Create typeless textures, unless it is a depth/stencil resource,
         * because D3D11_BIND_DEPTH_STENCIL requires a depth/stencil format.
         * Use explicit format for screen targets. For example they can be used
         * for video processor output view, which does not allow a typeless format.
         */
        DXGI_FORMAT const dxgiFormatTypeless = dxGetDxgiTypelessFormat(dxgiFormat);
        if (   !dxIsDepthStencilFormat(dxgiFormat)
            && !RT_BOOL(pSurface->f.surfaceFlags & SVGA3D_SURFACE_SCREENTARGET))
            dxgiFormat = dxgiFormatTypeless;

        /*
         * Create D3D11 texture object.
         *
         * No initial data for multisample resources.
         * On NVidia the host driver does not allow initial data for large textures with D3D11_BIND_DECODER flag.
         */
        D3D11_SUBRESOURCE_DATA *paInitialData = NULL;
        if (   pSurface->paMipmapLevels[0].pSurfaceData
            && pSurface->surfaceDesc.multisampleCount <= 1
            && (BindFlags & D3D11_BIND_DECODER) == 0
           )
        {
            /* Can happen for a non GBO surface or if GBO texture was updated prior to creation of the hardware resource. */
            uint32_t const cSubresource = pSurface->cLevels * pSurface->surfaceDesc.numArrayElements;
            paInitialData = (D3D11_SUBRESOURCE_DATA *)RTMemAlloc(cSubresource * sizeof(D3D11_SUBRESOURCE_DATA));
            AssertPtrReturn(paInitialData, VERR_NO_MEMORY);

            for (uint32_t i = 0; i < cSubresource; ++i)
            {
                PVMSVGA3DMIPMAPLEVEL pMipmapLevel = &pSurface->paMipmapLevels[i];
                D3D11_SUBRESOURCE_DATA *p = &paInitialData[i];
                p->pSysMem          = pMipmapLevel->pSurfaceData;
                p->SysMemPitch      = pMipmapLevel->cbSurfacePitch;
                p->SysMemSlicePitch = pMipmapLevel->cbSurfacePlane;
            }
        }

#ifdef LOG_ENABLED
        uint32_t const cWidth = pSurface->paMipmapLevels[0].cBlocksX * pSurface->cxBlock;
        uint32_t const cHeight = pSurface->paMipmapLevels[0].cBlocksY * pSurface->cyBlock;
        uint32_t const cDepth = pSurface->paMipmapLevels[0].mipmapSize.depth;
        LogFlowFunc(("sid = %u %ux%ux%u mips = %u fmt = %s(%u) typeless = %s(%u) pInitData = %p\n",
                     pSurface->id, cWidth, cHeight, cDepth, pSurface->cLevels,
                     dxFormatName(dxgiFormat), dxgiFormat, dxFormatName(dxgiFormatTypeless), dxgiFormatTypeless,
                     paInitialData));
#endif

        hr = dxCreateTexture(pDXDevice, pSurface, /* fIntermediate*/ false, paInitialData,
                             dxgiFormat, Usage, BindFlags, CPUAccessFlags, MiscFlags,
                             &pBackendSurface->u.pResource, &pBackendSurface->enmResType);
        Assert(SUCCEEDED(hr));
        if (SUCCEEDED(hr))
        {
            LogFunc(("sid = %u\n", pSurface->id));
            pBackendSurface->enmDxgiFormat = dxgiFormat;
            pBackendSurface->enmD3D11Usage = Usage;
        }

        RTMemFree(paInitialData);
    }

    if (hr == DXGI_ERROR_DEVICE_REMOVED)
    {
        DEBUG_BREAKPOINT_TEST();
        hr = pDXDevice->pDevice->GetDeviceRemovedReason();
    }

    if (SUCCEEDED(hr))
    {
        /*
         * Success.
         */
        pSurface->pBackendSurface = pBackendSurface;
        return VINF_SUCCESS;
    }

    /* Failure. */
    D3D_RELEASE(pBackendSurface->u.pResource);
    RTMemFree(pBackendSurface);
    return VERR_NO_MEMORY;
}


static int dxEnsureResource(PVGASTATECC pThisCC, uint32_t sid,
                            PVMSVGA3DSURFACE *ppSurface, ID3D11Resource **ppResource)
{
    /* Get corresponding resource for sid. Create the surface if does not yet exist. */
    PVMSVGA3DSURFACE pSurface;
    int rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, sid, &pSurface);
    AssertRCReturn(rc, rc);

    if (pSurface->pBackendSurface == NULL)
    {
        /* Create the actual texture or buffer. */
        rc = vmsvga3dBackSurfaceCreateResource(pThisCC, pSurface);
        AssertRCReturn(rc, rc);
        LogFunc(("Created for sid = %u\n", sid));
    }

    ID3D11Resource *pResource = dxResource(pSurface);
    AssertReturn(pResource, VERR_INVALID_STATE);

    *ppSurface = pSurface;
    *ppResource  = pResource;
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackInit(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC)
{
    RT_NOREF(pDevIns, pThis);

    int rc;
#ifdef RT_OS_LINUX /** @todo Remove, this is currently needed for loading the X11 library in order to call XInitThreads(). */
    rc = glLdrInit(pDevIns);
    if (RT_FAILURE(rc))
    {
        LogRel(("VMSVGA3d: Error loading OpenGL library and resolving necessary functions: %Rrc\n", rc));
        return rc;
    }
#endif

    PVMSVGA3DBACKEND pBackend = (PVMSVGA3DBACKEND)RTMemAllocZ(sizeof(VMSVGA3DBACKEND));
    AssertReturn(pBackend, VERR_NO_MEMORY);
    pThisCC->svga.p3dState->pBackend = pBackend;

    rc = dxLoadD3D11Library(&pBackend->hD3D11);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        rc = RTLdrGetSymbol(pBackend->hD3D11, "D3D11CreateDevice", (void **)&pBackend->pfnD3D11CreateDevice);
        AssertRC(rc);
    }

#ifdef RT_OS_WINDOWS
    if (RT_SUCCESS(rc))
    {
        /* Failure to load the shader disassembler is ignored. */
        int rc2 = RTLdrLoadSystem("D3DCompiler_47", /* fNoUnload = */ true, &pBackend->hD3DCompiler);
        if (RT_SUCCESS(rc2))
            rc2 = RTLdrGetSymbol(pBackend->hD3DCompiler, "D3DDisassemble", (void **)&pBackend->pfnD3DDisassemble);
        Log6Func(("Load D3DDisassemble: %Rrc\n", rc2));
    }
#endif

    vmsvga3dDXInitContextMobData(&pBackend->svgaDXContext);
//DEBUG_BREAKPOINT_TEST();
    return rc;
}


static DECLCALLBACK(int) vmsvga3dBackPowerOn(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC)
{
    RT_NOREF(pDevIns, pThis);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    PVMSVGA3DBACKEND pBackend = pState->pBackend;
    AssertReturn(pBackend, VERR_INVALID_STATE);

    int rc = dxDeviceCreate(pBackend, &pBackend->dxDevice);
    if (RT_SUCCESS(rc))
    {
        IDXGIAdapter *pAdapter = NULL;
        HRESULT hr = pBackend->dxDevice.pDxgiFactory->EnumAdapters(0, &pAdapter);
        if (SUCCEEDED(hr))
        {
            DXGI_ADAPTER_DESC desc;
            hr = pAdapter->GetDesc(&desc);
            if (SUCCEEDED(hr))
            {
                pBackend->VendorId = desc.VendorId;
                pBackend->DeviceId = desc.DeviceId;

                char sz[RT_ELEMENTS(desc.Description)];
                for (unsigned i = 0; i < RT_ELEMENTS(desc.Description); ++i)
                    sz[i] = (char)desc.Description[i];
                LogRelMax(1, ("VMSVGA: Adapter %04x:%04x [%s]\n", pBackend->VendorId, pBackend->DeviceId, sz));
            }

            pAdapter->Release();
        }

        if (pBackend->dxDevice.pVideoDevice)
            dxLogRelVideoCaps(pBackend->dxDevice.pVideoDevice);

        if (!pThis->svga.fVMSVGA3dMSAA)
            pBackend->dxDevice.MultisampleCountMask = 0;
    }
    return rc;
}


static DECLCALLBACK(int) vmsvga3dBackReset(PVGASTATECC pThisCC)
{
    RT_NOREF(pThisCC);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackTerminate(PVGASTATECC pThisCC)
{
    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    if (pState->pBackend)
        dxDeviceDestroy(pState->pBackend, &pState->pBackend->dxDevice);

    return VINF_SUCCESS;
}


/** @todo Such structures must be in VBoxVideo3D.h */
typedef struct VBOX3DNOTIFYDEFINESCREEN
{
    VBOX3DNOTIFY Core;
    uint32_t cWidth;
    uint32_t cHeight;
    int32_t  xRoot;
    int32_t  yRoot;
    uint32_t fPrimary;
    uint32_t cDpi;
} VBOX3DNOTIFYDEFINESCREEN;


static int vmsvga3dDrvNotifyDefineScreen(PVGASTATECC pThisCC, VMSVGASCREENOBJECT *pScreen)
{
    VBOX3DNOTIFYDEFINESCREEN n;
    n.Core.enmNotification = VBOX3D_NOTIFY_TYPE_HW_SCREEN_CREATED;
    n.Core.iDisplay        = pScreen->idScreen;
    n.Core.u32Reserved     = 0;
    n.Core.cbData          = sizeof(n) - RT_UOFFSETOF(VBOX3DNOTIFY, au8Data);
    RT_ZERO(n.Core.au8Data);
    n.cWidth               = pScreen->cWidth;
    n.cHeight              = pScreen->cHeight;
    n.xRoot                = pScreen->xOrigin;
    n.yRoot                = pScreen->yOrigin;
    n.fPrimary             = RT_BOOL(pScreen->fuScreen & SVGA_SCREEN_IS_PRIMARY);
    n.cDpi                 = pScreen->cDpi;

    return pThisCC->pDrv->pfn3DNotifyProcess(pThisCC->pDrv, &n.Core);
}


static int vmsvga3dDrvNotifyDestroyScreen(PVGASTATECC pThisCC, VMSVGASCREENOBJECT *pScreen)
{
    VBOX3DNOTIFY n;
    n.enmNotification = VBOX3D_NOTIFY_TYPE_HW_SCREEN_DESTROYED;
    n.iDisplay        = pScreen->idScreen;
    n.u32Reserved     = 0;
    n.cbData          = sizeof(n) - RT_UOFFSETOF(VBOX3DNOTIFY, au8Data);
    RT_ZERO(n.au8Data);

    return pThisCC->pDrv->pfn3DNotifyProcess(pThisCC->pDrv, &n);
}


static int vmsvga3dDrvNotifyBindSurface(PVGASTATECC pThisCC, VMSVGASCREENOBJECT *pScreen, HANDLE hSharedSurface)
{
    VBOX3DNOTIFY n;
    n.enmNotification = VBOX3D_NOTIFY_TYPE_HW_SCREEN_BIND_SURFACE;
    n.iDisplay        = pScreen->idScreen;
    n.u32Reserved     = 0;
    n.cbData          = sizeof(n) - RT_UOFFSETOF(VBOX3DNOTIFY, au8Data);
    *(uint64_t *)&n.au8Data[0] = (uint64_t)hSharedSurface;

    return pThisCC->pDrv->pfn3DNotifyProcess(pThisCC->pDrv, &n);
}


typedef struct VBOX3DNOTIFYUPDATE
{
    VBOX3DNOTIFY Core;
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
} VBOX3DNOTIFYUPDATE;


static int vmsvga3dDrvNotifyUpdate(PVGASTATECC pThisCC, VMSVGASCREENOBJECT *pScreen,
                                   uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    VBOX3DNOTIFYUPDATE n;
    n.Core.enmNotification = VBOX3D_NOTIFY_TYPE_HW_SCREEN_UPDATE_END;
    n.Core.iDisplay        = pScreen->idScreen;
    n.Core.u32Reserved     = 0;
    n.Core.cbData          = sizeof(n) - RT_UOFFSETOF(VBOX3DNOTIFY, au8Data);
    RT_ZERO(n.Core.au8Data);
    n.x = x;
    n.y = y;
    n.w = w;
    n.h = h;

    return pThisCC->pDrv->pfn3DNotifyProcess(pThisCC->pDrv, &n.Core);
}

static int vmsvga3dHwScreenCreate(PVMSVGA3DSTATE pState, uint32_t cWidth, uint32_t cHeight, VMSVGAHWSCREEN *p)
{
    PVMSVGA3DBACKEND pBackend = pState->pBackend;

    DXDEVICE *pDXDevice = &pBackend->dxDevice;
    AssertReturn(pDXDevice->pDevice, VERR_INVALID_STATE);

    D3D11_TEXTURE2D_DESC td;
    RT_ZERO(td);
    td.Width              = cWidth;
    td.Height             = cHeight;
    td.MipLevels          = 1;
    td.ArraySize          = 1;
    td.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count   = 1;
    td.SampleDesc.Quality = 0;
    td.Usage              = D3D11_USAGE_DEFAULT;
    td.BindFlags          = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags     = 0;
    td.MiscFlags          = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    HRESULT hr = pDXDevice->pDevice->CreateTexture2D(&td, 0, &p->pTexture);
    if (SUCCEEDED(hr))
    {
        /* Get the shared handle. */
        hr = p->pTexture->QueryInterface(__uuidof(IDXGIResource), (void**)&p->pDxgiResource);
        if (SUCCEEDED(hr))
        {
            hr = p->pDxgiResource->GetSharedHandle(&p->SharedHandle);
            if (SUCCEEDED(hr))
                hr = p->pTexture->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&p->pDXGIKeyedMutex);
        }
    }

    if (SUCCEEDED(hr))
        return VINF_SUCCESS;

    AssertFailed();
    return VERR_NOT_SUPPORTED;
}


static void vmsvga3dHwScreenDestroy(PVMSVGA3DSTATE pState, VMSVGAHWSCREEN *p)
{
    RT_NOREF(pState);
    D3D_RELEASE(p->pDXGIKeyedMutex);
    D3D_RELEASE(p->pDxgiResource);
    D3D_RELEASE(p->pTexture);
    p->SharedHandle = 0;
    p->sidScreenTarget = SVGA_ID_INVALID;
}


static DECLCALLBACK(int) vmsvga3dBackDefineScreen(PVGASTATE pThis, PVGASTATECC pThisCC, VMSVGASCREENOBJECT *pScreen)
{
    RT_NOREF(pThis, pThisCC, pScreen);

    LogRel4(("VMSVGA: vmsvga3dBackDefineScreen: screen %u\n", pScreen->idScreen));

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    PVMSVGA3DBACKEND pBackend = pState->pBackend;
    AssertReturn(pBackend, VERR_INVALID_STATE);

    Assert(pScreen->pHwScreen == NULL);

    VMSVGAHWSCREEN *p = (VMSVGAHWSCREEN *)RTMemAllocZ(sizeof(VMSVGAHWSCREEN));
    AssertPtrReturn(p, VERR_NO_MEMORY);

    p->sidScreenTarget = SVGA_ID_INVALID;

    int rc = vmsvga3dDrvNotifyDefineScreen(pThisCC, pScreen);
    if (RT_SUCCESS(rc))
    {
        /* The frontend supports the screen. Create the actual resource. */
        rc = vmsvga3dHwScreenCreate(pState, pScreen->cWidth, pScreen->cHeight, p);
        if (RT_SUCCESS(rc))
            LogRel4(("VMSVGA: vmsvga3dBackDefineScreen: created\n"));
    }

    if (RT_SUCCESS(rc))
    {
        LogRel(("VMSVGA: Using HW accelerated screen %u\n", pScreen->idScreen));
        pScreen->pHwScreen = p;
    }
    else
    {
        LogRel4(("VMSVGA: vmsvga3dBackDefineScreen: %Rrc\n", rc));
        vmsvga3dHwScreenDestroy(pState, p);
        RTMemFree(p);
    }

    return rc;
}


static DECLCALLBACK(int) vmsvga3dBackDestroyScreen(PVGASTATECC pThisCC, VMSVGASCREENOBJECT *pScreen)
{
    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    vmsvga3dDrvNotifyDestroyScreen(pThisCC, pScreen);

    if (pScreen->pHwScreen)
    {
        vmsvga3dHwScreenDestroy(pState, pScreen->pHwScreen);
        RTMemFree(pScreen->pHwScreen);
        pScreen->pHwScreen = NULL;
    }

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackSurfaceBlitToScreen(PVGASTATECC pThisCC, VMSVGASCREENOBJECT *pScreen,
                                    SVGASignedRect destRect, SVGA3dSurfaceImageId srcImage,
                                    SVGASignedRect srcRect, uint32_t cRects, SVGASignedRect *paRects)
{
    RT_NOREF(pThisCC, pScreen, destRect, srcImage, srcRect, cRects, paRects);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    PVMSVGA3DBACKEND pBackend = pState->pBackend;
    AssertReturn(pBackend, VERR_INVALID_STATE);

    VMSVGAHWSCREEN *p = pScreen->pHwScreen;
    AssertReturn(p, VERR_NOT_SUPPORTED);

    PVMSVGA3DSURFACE pSurface;
    int rc = vmsvga3dSurfaceFromSid(pState, srcImage.sid, &pSurface);
    AssertRCReturn(rc, rc);

    /** @todo Implement. */
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackSurfaceMap(PVGASTATECC pThisCC, SVGA3dSurfaceImageId const *pImage, SVGA3dBox const *pBox,
                                                VMSVGA3D_SURFACE_MAP enmMapType, uint32_t fMapFlags, VMSVGA3D_MAPPED_SURFACE *pMap)
{
    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    PVMSVGA3DBACKEND pBackend = pState->pBackend;
    AssertReturn(pBackend, VERR_INVALID_STATE);

    PVMSVGA3DSURFACE pSurface;
    int rc = vmsvga3dSurfaceFromSid(pState, pImage->sid, &pSurface);
    AssertRCReturn(rc, rc);

    PVMSVGA3DBACKENDSURFACE pBackendSurface = pSurface->pBackendSurface;
    AssertPtrReturn(pBackendSurface, VERR_INVALID_STATE);

    PVMSVGA3DMIPMAPLEVEL pMipLevel;
    rc = vmsvga3dMipmapLevel(pSurface, pImage->face, pImage->mipmap, &pMipLevel);
    ASSERT_GUEST_RETURN(RT_SUCCESS(rc), rc);

    DXDEVICE *pDevice = &pBackend->dxDevice;
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    SVGA3dBox clipBox;
    if (pBox)
    {
        clipBox = *pBox;
        vmsvgaR3ClipBox(&pMipLevel->mipmapSize, &clipBox);
        ASSERT_GUEST_RETURN(clipBox.w && clipBox.h && clipBox.d, VERR_INVALID_PARAMETER);
    }
    else
    {
        clipBox.x = 0;
        clipBox.y = 0;
        clipBox.z = 0;
        clipBox.w = pMipLevel->mipmapSize.width;
        clipBox.h = pMipLevel->mipmapSize.height;
        clipBox.d = pMipLevel->mipmapSize.depth;
    }

    D3D11_MAP d3d11MapType;
    switch (enmMapType)
    {
        case VMSVGA3D_SURFACE_MAP_READ:          d3d11MapType = D3D11_MAP_READ; break;
        case VMSVGA3D_SURFACE_MAP_WRITE:         d3d11MapType = D3D11_MAP_WRITE; break;
        case VMSVGA3D_SURFACE_MAP_READ_WRITE:    d3d11MapType = D3D11_MAP_READ_WRITE; break;
        case VMSVGA3D_SURFACE_MAP_WRITE_DISCARD: d3d11MapType = D3D11_MAP_WRITE_DISCARD; break;
        default:
            AssertFailedReturn(VERR_INVALID_PARAMETER);
    }

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    RT_ZERO(mappedResource);

    ID3D11Resource *pMappedResource;
    D3D11_USAGE mappedResourceUsage;
    if (fMapFlags & VMSVGA3D_MAP_F_DYNAMIC_INTERMEDIATE)
    {
        mappedResourceUsage = D3D11_USAGE_DYNAMIC;
        pMappedResource = pBackendSurface->dynamic.pResource;
    }
    else if (fMapFlags & VMSVGA3D_MAP_F_STAGING_INTERMEDIATE)
    {
        mappedResourceUsage = D3D11_USAGE_STAGING;
        pMappedResource = pBackendSurface->staging.pResource;
    }
    else if (d3d11MapType == D3D11_MAP_READ)
    {
        mappedResourceUsage = D3D11_USAGE_STAGING;
        if (pBackendSurface->enmD3D11Usage == D3D11_USAGE_STAGING)
            pMappedResource = pBackendSurface->u.pResource;
        else
            pMappedResource = pBackendSurface->staging.pResource;
    }
    else if (d3d11MapType == D3D11_MAP_WRITE)
    {
        mappedResourceUsage = D3D11_USAGE_STAGING;
        if (pBackendSurface->enmD3D11Usage == D3D11_USAGE_STAGING)
            pMappedResource = pBackendSurface->u.pResource;
        else
            pMappedResource = pBackendSurface->staging.pResource;
    }
    else
    {
        mappedResourceUsage = D3D11_USAGE_DYNAMIC;
        if (pBackendSurface->enmD3D11Usage == D3D11_USAGE_DYNAMIC)
            pMappedResource = pBackendSurface->u.pResource;
        else
            pMappedResource = pBackendSurface->dynamic.pResource;
    }

    if (   pBackendSurface->enmResType == VMSVGA3D_RESTYPE_TEXTURE_1D
        || pBackendSurface->enmResType == VMSVGA3D_RESTYPE_TEXTURE_2D
        || pBackendSurface->enmResType == VMSVGA3D_RESTYPE_TEXTURE_CUBE
        || pBackendSurface->enmResType == VMSVGA3D_RESTYPE_TEXTURE_3D)
    {
        UINT const Subresource = D3D11CalcSubresource(pImage->mipmap, pImage->face, pSurface->cLevels);

        if (!pMappedResource)
        {
            /* Create an intermediate resource. */
            D3D11_SUBRESOURCE_DATA *paInitialData = NULL;

            /* Always use typeless format for staging/dynamic resources. */
            DXGI_FORMAT Format = dxGetDxgiTypelessFormat(pBackendSurface->enmDxgiFormat);
            if (pBackend->VendorId == 0x1002 && Format == DXGI_FORMAT_R24G8_TYPELESS)
            {
                /* AMD driver does not allow DXGI_FORMAT_R24G8_TYPELESS for dynamic resources. */
                Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            }

            UINT BindFlags = 0;
            if (mappedResourceUsage == D3D11_USAGE_DYNAMIC)
                BindFlags |= D3D11_BIND_SHADER_RESOURCE; /* Have to specify a supported flag, otherwise E_INVALIDARG will be returned. */

            UINT CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (mappedResourceUsage == D3D11_USAGE_STAGING)
                CPUAccessFlags |= D3D11_CPU_ACCESS_READ;

            UINT MiscFlags = 0;

            VMSVGA3DBACKRESTYPE resourceType = VMSVGA3D_RESTYPE_NONE;

            HRESULT hr = dxCreateTexture(pDevice, pSurface, /* fIntermediate*/ true, paInitialData,
                                         Format, mappedResourceUsage, BindFlags, CPUAccessFlags, MiscFlags,
                                         &pMappedResource, &resourceType);
            if (SUCCEEDED(hr))
            {
                Assert(resourceType == pBackendSurface->enmResType);

                if (mappedResourceUsage == D3D11_USAGE_DYNAMIC)
                    pBackendSurface->dynamic.pResource = pMappedResource;
                else if (mappedResourceUsage == D3D11_USAGE_STAGING)
                    pBackendSurface->staging.pResource = pMappedResource;
                else
                    AssertFailedReturnStmt(D3D_RELEASE(pMappedResource), VERR_INVALID_STATE);
            }
            else
                AssertFailedReturn(VERR_NO_MEMORY);
        }

        if (   pMappedResource != pBackendSurface->u.pResource
            && (d3d11MapType == D3D11_MAP_READ || d3d11MapType == D3D11_MAP_READ_WRITE))
        {
            /* Copy the texture content to the staging texture.
             * The requested miplevel of the texture is copied to the miplevel 0 of the staging texture,
             * because the staging (and dynamic) textures do not have miplevels.
             */
            ID3D11Resource *pDstResource = pMappedResource;
            UINT const DstSubresource = 0; /* Intermediate dynamic or staging textures have one subresource. */
            UINT DstX;
            UINT DstY;
            UINT DstZ;
            ID3D11Resource *pSrcResource = pBackendSurface->u.pResource;
            UINT const SrcSubresource = Subresource;
            D3D11_BOX SrcBox;
            D3D11_BOX *pSrcBox;
            if (RT_LIKELY(!RT_BOOL(fMapFlags & VMSVGA3D_MAP_F_EXACT_REGION)))
            {
                /* Copy entire miplevel so all Dst are zero and pSrcBox is NULL, as D3D11 requires. */
                DstX = 0;
                DstY = 0;
                DstZ = 0;
                pSrcBox = NULL;
            }
            else
            {
                DstX = (clipBox.x / pSurface->cxBlock) * pSurface->cxBlock;
                DstY = (clipBox.y / pSurface->cyBlock) * pSurface->cyBlock;
                DstZ = clipBox.z;

                uint32_t const cxBlocks = (clipBox.w + pSurface->cxBlock - 1) / pSurface->cxBlock;
                uint32_t const cyBlocks = (clipBox.h + pSurface->cyBlock - 1) / pSurface->cyBlock;

                SrcBox.left   = DstX;
                SrcBox.top    = DstY;
                SrcBox.front  = DstZ;
                SrcBox.right  = DstX + cxBlocks * pSurface->cxBlock;
                SrcBox.bottom = DstY + cyBlocks * pSurface->cyBlock;
                SrcBox.back   = DstZ + clipBox.d;
                pSrcBox = &SrcBox;
            }
            pDevice->pImmediateContext->CopySubresourceRegion(pDstResource, DstSubresource, DstX, DstY, DstZ,
                                                              pSrcResource, SrcSubresource, pSrcBox);
        }

        UINT const MappedSubresource = pMappedResource == pBackendSurface->u.pResource
                                       ? Subresource
                                       : 0; /* Intermediate dynamic or staging textures have one subresource. */
        HRESULT hr = pDevice->pImmediateContext->Map(pMappedResource, MappedSubresource,
                                                     d3d11MapType, /* MapFlags =  */ 0, &mappedResource);
        AssertReturn(SUCCEEDED(hr), VERR_NO_MEMORY);
    }
    else if (pBackendSurface->enmResType == VMSVGA3D_RESTYPE_BUFFER)
    {
        UINT const Subresource = 0; /* Buffers have one subresource. */

        if (!pMappedResource)
        {
            /* Create an intermediate buffer. */
            D3D11_SUBRESOURCE_DATA *paInitialData = NULL;

            UINT BindFlags = 0;
            if (mappedResourceUsage == D3D11_USAGE_DYNAMIC)
                BindFlags |= D3D11_BIND_SHADER_RESOURCE; /* Have to specify a supported flag, otherwise E_INVALIDARG will be returned. */

            UINT CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (mappedResourceUsage == D3D11_USAGE_STAGING)
                CPUAccessFlags |= D3D11_CPU_ACCESS_READ;

            D3D11_BUFFER_DESC bd;
            RT_ZERO(bd);
            bd.ByteWidth = pMipLevel->cbSurface;
            bd.Usage = mappedResourceUsage;
            bd.BindFlags = BindFlags;
            bd.CPUAccessFlags = CPUAccessFlags;
            //bd.MiscFlags = 0;
            //bd.StructureByteStride = 0;

            HRESULT hr = pDevice->pDevice->CreateBuffer(&bd, paInitialData, (ID3D11Buffer **)&pMappedResource);
            if (SUCCEEDED(hr))
            {
                if (mappedResourceUsage == D3D11_USAGE_DYNAMIC)
                    pBackendSurface->dynamic.pResource = pMappedResource;
                else if (mappedResourceUsage == D3D11_USAGE_STAGING)
                    pBackendSurface->staging.pResource = pMappedResource;
                else
                    AssertFailedReturnStmt(D3D_RELEASE(pMappedResource), VERR_INVALID_STATE);
            }
            else
                AssertFailedReturn(VERR_NO_MEMORY);
        }

        if (   pMappedResource != pBackendSurface->u.pResource
            && (d3d11MapType == D3D11_MAP_READ || d3d11MapType == D3D11_MAP_READ_WRITE))
        {
            /* Copy the resource content to the staging resource. */
            ID3D11Resource *pDstResource = pMappedResource;
            UINT const DstSubresource = 0;
            UINT const DstX = clipBox.x;
            UINT const DstY = clipBox.y;
            UINT const DstZ = clipBox.z;
            ID3D11Resource *pSrcResource = pBackendSurface->u.pResource;
            UINT const SrcSubresource = Subresource;
            D3D11_BOX SrcBox;
            SrcBox.left   = DstX;
            SrcBox.top    = DstY;
            SrcBox.front  = DstZ;
            SrcBox.right  = DstX + clipBox.w;
            SrcBox.bottom = DstY + clipBox.h;
            SrcBox.back   = DstZ + clipBox.d;
            pDevice->pImmediateContext->CopySubresourceRegion(pDstResource, DstSubresource, DstX, DstY, DstZ,
                                                              pSrcResource, SrcSubresource, &SrcBox);
        }

        HRESULT hr = pDevice->pImmediateContext->Map(pMappedResource, Subresource,
                                                     d3d11MapType, /* MapFlags =  */ 0, &mappedResource);
        AssertReturn(SUCCEEDED(hr), VERR_NO_MEMORY);
    }
    else
        AssertFailedReturn(VERR_NOT_IMPLEMENTED);

    Assert(RT_SUCCESS(rc));

    vmsvga3dSurfaceMapInit(pMap, enmMapType, fMapFlags, &clipBox, pSurface,
                           mappedResource.pData, mappedResource.RowPitch, mappedResource.DepthPitch);
    pMap->pvBackendResource = pMappedResource;

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackSurfaceUnmap(PVGASTATECC pThisCC, SVGA3dSurfaceImageId const *pImage, VMSVGA3D_MAPPED_SURFACE *pMap, bool fWritten)
{
    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    PVMSVGA3DBACKEND pBackend = pState->pBackend;
    AssertReturn(pBackend, VERR_INVALID_STATE);

    PVMSVGA3DSURFACE pSurface;
    int rc = vmsvga3dSurfaceFromSid(pState, pImage->sid, &pSurface);
    AssertRCReturn(rc, rc);

    /* The called should not use the function for system memory surfaces. */
    PVMSVGA3DBACKENDSURFACE pBackendSurface = pSurface->pBackendSurface;
    AssertReturn(pBackendSurface, VERR_INVALID_PARAMETER);

    PVMSVGA3DMIPMAPLEVEL pMipLevel;
    rc = vmsvga3dMipmapLevel(pSurface, pImage->face, pImage->mipmap, &pMipLevel);
    ASSERT_GUEST_RETURN(RT_SUCCESS(rc), rc);

    DXDEVICE *pDevice = &pBackend->dxDevice;
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    ID3D11Resource *pMappedResource = (ID3D11Resource *)pMap->pvBackendResource;

    if (   pBackendSurface->enmResType == VMSVGA3D_RESTYPE_TEXTURE_1D
        || pBackendSurface->enmResType == VMSVGA3D_RESTYPE_TEXTURE_2D
        || pBackendSurface->enmResType == VMSVGA3D_RESTYPE_TEXTURE_CUBE
        || pBackendSurface->enmResType == VMSVGA3D_RESTYPE_TEXTURE_3D)
    {
        UINT const Subresource = D3D11CalcSubresource(pImage->mipmap, pImage->face, pSurface->cLevels);

        UINT const MappedSubresource = pMappedResource == pBackendSurface->u.pResource
                                       ? Subresource
                                       : 0; /* Intermediate dynamic or staging textures have one subresource. */
        pDevice->pImmediateContext->Unmap(pMappedResource, MappedSubresource);

        if (   pMappedResource != pBackendSurface->u.pResource
            && fWritten
            && (   pMap->enmMapType == VMSVGA3D_SURFACE_MAP_WRITE
                || pMap->enmMapType == VMSVGA3D_SURFACE_MAP_READ_WRITE
                || pMap->enmMapType == VMSVGA3D_SURFACE_MAP_WRITE_DISCARD))
        {
            /* If entire resource must be copied then use pSrcBox = NULL and dst point (0,0,0)
             * Because DX11 insists on this for some resource types, for example DEPTH_STENCIL resources.
             */
            /** @todo DEPTH_STENCIL resources can be mapped only as entire resource. Implement updating a subregion of them. */
            uint32_t const cWidth0 = pSurface->paMipmapLevels[0].mipmapSize.width;
            uint32_t const cHeight0 = pSurface->paMipmapLevels[0].mipmapSize.height;
            uint32_t const cDepth0 = pSurface->paMipmapLevels[0].mipmapSize.depth;
            bool const fEntireResource = pMap->box.x == 0 && pMap->box.y == 0 && pMap->box.z == 0
                                      && pMap->box.w == cWidth0 && pMap->box.h == cHeight0 && pMap->box.d == cDepth0;

            ID3D11Resource *pDstResource = pBackendSurface->u.pResource;
            UINT const DstSubresource = Subresource;
            UINT DstX;
            UINT DstY;
            UINT DstZ;
            ID3D11Resource *pSrcResource = pMappedResource;
            UINT const SrcSubresource = 0; /* Intermediate dynamic or staging textures have one subresource. */
            D3D11_BOX *pSrcBox;
            D3D11_BOX SrcBox;
            if (fEntireResource)
            {
                DstX = 0;
                DstY = 0;
                DstZ = 0;
                pSrcBox = NULL;
            }
            else
            {
                DstX = (pMap->box.x / pSurface->cxBlock) * pSurface->cxBlock;
                DstY = (pMap->box.y / pSurface->cyBlock) * pSurface->cyBlock;
                DstZ = pMap->box.z;

                uint32_t const cxBlocks = (pMap->box.w + pSurface->cxBlock - 1) / pSurface->cxBlock;
                uint32_t const cyBlocks = (pMap->box.h + pSurface->cyBlock - 1) / pSurface->cyBlock;

                SrcBox.left   = DstX;
                SrcBox.top    = DstY;
                SrcBox.front  = DstZ;
                SrcBox.right  = DstX + cxBlocks * pSurface->cxBlock;
                SrcBox.bottom = DstY + cyBlocks * pSurface->cyBlock;
                SrcBox.back   = DstZ + pMap->box.d;
                pSrcBox = &SrcBox;
            }

            pDevice->pImmediateContext->CopySubresourceRegion(pDstResource, DstSubresource, DstX, DstY, DstZ,
                                                              pSrcResource, SrcSubresource, pSrcBox);
        }
    }
    else if (pBackendSurface->enmResType == VMSVGA3D_RESTYPE_BUFFER)
    {
        Log4(("Unmap buffer sid = %u:\n%.*Rhxd\n", pSurface->id, pMap->cbRow, pMap->pvData));

        UINT const Subresource = 0; /* Buffers have one subresource. */

        pDevice->pImmediateContext->Unmap(pMappedResource, Subresource);

        if (   pMappedResource != pBackendSurface->u.pResource
            && fWritten
            && (   pMap->enmMapType == VMSVGA3D_SURFACE_MAP_WRITE
                || pMap->enmMapType == VMSVGA3D_SURFACE_MAP_READ_WRITE
                || pMap->enmMapType == VMSVGA3D_SURFACE_MAP_WRITE_DISCARD))
        {
            ID3D11Resource *pDstResource = pBackendSurface->u.pResource;
            UINT DstSubresource = Subresource;
            UINT DstX = pMap->box.x;
            UINT DstY = pMap->box.y;
            UINT DstZ = pMap->box.z;
            ID3D11Resource *pSrcResource = pMappedResource;
            UINT SrcSubresource = 0;
            D3D11_BOX SrcBox;
            SrcBox.left   = DstX;
            SrcBox.top    = DstY;
            SrcBox.front  = DstZ;
            SrcBox.right  = DstX + pMap->box.w;
            SrcBox.bottom = DstY + pMap->box.h;
            SrcBox.back   = DstZ + pMap->box.d;
            pDevice->pImmediateContext->CopySubresourceRegion(pDstResource, DstSubresource, DstX, DstY, DstZ,
                                                              pSrcResource, SrcSubresource, &SrcBox);
        }
    }
    else
        AssertFailedReturn(VERR_NOT_IMPLEMENTED);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dScreenTargetBind(PVGASTATECC pThisCC, VMSVGASCREENOBJECT *pScreen, uint32_t sid)
{
    int rc = VINF_SUCCESS;

    PVMSVGA3DSURFACE pSurface;
    if (sid != SVGA_ID_INVALID)
    {
        /* Create the surface if does not yet exist. */
        PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
        AssertReturn(pState, VERR_INVALID_STATE);

        rc = vmsvga3dSurfaceFromSid(pState, sid, &pSurface);
        AssertRCReturn(rc, rc);

        if (!VMSVGA3DSURFACE_HAS_HW_SURFACE(pSurface) && !pState->fVMSVGA2dGBO)
        {
            /* Create the actual texture. */
            rc = vmsvga3dBackSurfaceCreateResource(pThisCC, pSurface);
            AssertRCReturn(rc, rc);
        }
    }
    else
        pSurface = NULL;

    /* Notify the HW accelerated screen if it is used. */
    VMSVGAHWSCREEN *pHwScreen = pScreen->pHwScreen;
    if (!pHwScreen)
        return VINF_SUCCESS;

    /* Same surface -> do nothing. */
    if (pHwScreen->sidScreenTarget == sid)
        return VINF_SUCCESS;

    if (sid != SVGA_ID_INVALID)
    {
        AssertReturn(   pSurface->pBackendSurface
                     && pSurface->pBackendSurface->enmResType == VMSVGA3D_RESTYPE_TEXTURE_2D
                     && RT_BOOL(pSurface->f.surfaceFlags & SVGA3D_SURFACE_SCREENTARGET), VERR_INVALID_PARAMETER);

        HANDLE const hSharedSurface = pHwScreen->SharedHandle;
        rc = vmsvga3dDrvNotifyBindSurface(pThisCC, pScreen, hSharedSurface);
    }

    if (RT_SUCCESS(rc))
    {
        pHwScreen->sidScreenTarget = sid;
    }

    return rc;
}


static DECLCALLBACK(int) vmsvga3dScreenTargetUpdate(PVGASTATECC pThisCC, VMSVGASCREENOBJECT *pScreen, SVGA3dRect const *pRect)
{
    VMSVGAHWSCREEN *pHwScreen = pScreen->pHwScreen;
    AssertReturn(pHwScreen, VERR_NOT_SUPPORTED);

    if (pHwScreen->sidScreenTarget == SVGA_ID_INVALID)
        return VINF_SUCCESS; /* No surface bound. */

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    PVMSVGA3DBACKEND pBackend = pState->pBackend;
    AssertReturn(pBackend, VERR_INVALID_STATE);

    PVMSVGA3DSURFACE pSurface;
    int rc = vmsvga3dSurfaceFromSid(pState, pHwScreen->sidScreenTarget, &pSurface);
    AssertRCReturn(rc, rc);

    PVMSVGA3DBACKENDSURFACE pBackendSurface = pSurface->pBackendSurface;
    AssertReturn(   pBackendSurface
                 && pBackendSurface->enmResType == VMSVGA3D_RESTYPE_TEXTURE_2D
                 && RT_BOOL(pSurface->f.surfaceFlags & SVGA3D_SURFACE_SCREENTARGET),
                 VERR_INVALID_PARAMETER);

    SVGA3dRect boundRect;
    boundRect.x = 0;
    boundRect.y = 0;
    boundRect.w = pSurface->paMipmapLevels[0].mipmapSize.width;
    boundRect.h = pSurface->paMipmapLevels[0].mipmapSize.height;
    SVGA3dRect clipRect = *pRect;
    vmsvgaR3Clip3dRect(&boundRect, &clipRect);
    ASSERT_GUEST_RETURN(clipRect.w && clipRect.h, VERR_INVALID_PARAMETER);

    /* Copy the screen texture to the shared surface. */
    DWORD result = pHwScreen->pDXGIKeyedMutex->AcquireSync(0, 10000);
    if (result == S_OK)
    {
        pBackend->dxDevice.pImmediateContext->CopyResource(pHwScreen->pTexture, pBackendSurface->u.pTexture2D);

        dxDeviceFlush(&pBackend->dxDevice);

        result = pHwScreen->pDXGIKeyedMutex->ReleaseSync(1);
    }
    else
        AssertFailed();

    rc = vmsvga3dDrvNotifyUpdate(pThisCC, pScreen, pRect->x, pRect->y, pRect->w, pRect->h);
    return rc;
}


/*
 *
 * 3D interface.
 *
 */

static DECLCALLBACK(int) vmsvga3dBackQueryCaps(PVGASTATECC pThisCC, SVGA3dDevCapIndex idx3dCaps, uint32_t *pu32Val)
{
    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    int rc = VINF_SUCCESS;

    *pu32Val = 0;

    if (idx3dCaps > SVGA3D_DEVCAP_MAX)
    {
        LogRelMax(16, ("VMSVGA: unsupported SVGA3D_DEVCAP %d\n", idx3dCaps));
        return VERR_NOT_SUPPORTED;
    }

    D3D_FEATURE_LEVEL const FeatureLevel = pState->pBackend->dxDevice.FeatureLevel;

    /* Most values are taken from:
     * https://docs.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-devices-downlevel-intro
     *
     * Shader values are from
     * https://docs.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-models
     */

    switch (idx3dCaps)
    {
    case SVGA3D_DEVCAP_3D:
        *pu32Val = VBSVGA3D_CAP_3D;
        if (pState->pBackend->dxDevice.pVideoDevice)
            *pu32Val |= VBSVGA3D_CAP_VIDEO;
#if !defined(VBOX_WITH_DXVK)
        /* D3D11_RASTERIZER_DESC1::ForcedSampleCount does not work with dxvk (at least up to 2.6.2).
         * Therefore only 11.0 level can be supported in the guest. */
        if (FeatureLevel >= D3D_FEATURE_LEVEL_11_1)
            *pu32Val |= VBSVGA3D_CAP_RASTERIZER_STATE_V2;
#endif
        break;

    case SVGA3D_DEVCAP_MAX_LIGHTS:
        *pu32Val = SVGA3D_NUM_LIGHTS; /* VGPU9. Not applicable to DX11. */
        break;

    case SVGA3D_DEVCAP_MAX_TEXTURES:
        *pu32Val = SVGA3D_NUM_TEXTURE_UNITS; /* VGPU9. Not applicable to DX11. */
        break;

    case SVGA3D_DEVCAP_MAX_CLIP_PLANES:
        *pu32Val = SVGA3D_NUM_CLIPPLANES;
        break;

    case SVGA3D_DEVCAP_VERTEX_SHADER_VERSION:
        if (FeatureLevel >= D3D_FEATURE_LEVEL_10_0)
            *pu32Val = SVGA3DVSVERSION_40;
        else
            *pu32Val = SVGA3DVSVERSION_30;
        break;

    case SVGA3D_DEVCAP_VERTEX_SHADER:
        *pu32Val = 1;
        break;

    case SVGA3D_DEVCAP_FRAGMENT_SHADER_VERSION:
        if (FeatureLevel >= D3D_FEATURE_LEVEL_10_0)
            *pu32Val = SVGA3DPSVERSION_40;
        else
            *pu32Val = SVGA3DPSVERSION_30;
        break;

    case SVGA3D_DEVCAP_FRAGMENT_SHADER:
        *pu32Val = 1;
        break;

    case SVGA3D_DEVCAP_MAX_RENDER_TARGETS:
        if (FeatureLevel >= D3D_FEATURE_LEVEL_10_0)
            *pu32Val = 8;
        else
            *pu32Val = 4;
        break;

    case SVGA3D_DEVCAP_S23E8_TEXTURES:
    case SVGA3D_DEVCAP_S10E5_TEXTURES:
        /* Must be obsolete by now; surface format caps specify the same thing. */
        break;

    case SVGA3D_DEVCAP_MAX_FIXED_VERTEXBLEND:
        /* Obsolete */
        break;

    /*
     *   2. The BUFFER_FORMAT capabilities are deprecated, and they always
     *      return TRUE. Even on physical hardware that does not support
     *      these formats natively, the SVGA3D device will provide an emulation
     *      which should be invisible to the guest OS.
     */
    case SVGA3D_DEVCAP_D16_BUFFER_FORMAT:
    case SVGA3D_DEVCAP_D24S8_BUFFER_FORMAT:
    case SVGA3D_DEVCAP_D24X8_BUFFER_FORMAT:
        *pu32Val = 1;
        break;

    case SVGA3D_DEVCAP_QUERY_TYPES:
        /* Obsolete */
        break;

    case SVGA3D_DEVCAP_TEXTURE_GRADIENT_SAMPLING:
        /* Obsolete */
        break;

    case SVGA3D_DEVCAP_MAX_POINT_SIZE:
        AssertCompile(sizeof(uint32_t) == sizeof(float));
        *(float *)pu32Val = 256.0f;  /* VGPU9. Not applicable to DX11. */
        break;

    case SVGA3D_DEVCAP_MAX_SHADER_TEXTURES:
        /* Obsolete */
        break;

    case SVGA3D_DEVCAP_MAX_TEXTURE_WIDTH:
    case SVGA3D_DEVCAP_MAX_TEXTURE_HEIGHT:
        if (FeatureLevel >= D3D_FEATURE_LEVEL_11_0)
            *pu32Val = 16384;
        else if (FeatureLevel >= D3D_FEATURE_LEVEL_10_0)
            *pu32Val = 8192;
        else if (FeatureLevel >= D3D_FEATURE_LEVEL_9_3)
            *pu32Val = 4096;
        else
            *pu32Val = 2048;
        break;

    case SVGA3D_DEVCAP_MAX_VOLUME_EXTENT:
        if (FeatureLevel >= D3D_FEATURE_LEVEL_10_0)
            *pu32Val = 2048;
        else
            *pu32Val = 256;
        break;

    case SVGA3D_DEVCAP_MAX_TEXTURE_REPEAT:
        if (FeatureLevel >= D3D_FEATURE_LEVEL_11_0)
            *pu32Val = 16384;
        else if (FeatureLevel >= D3D_FEATURE_LEVEL_9_3)
            *pu32Val = 8192;
        else if (FeatureLevel >= D3D_FEATURE_LEVEL_9_2)
            *pu32Val = 2048;
        else
            *pu32Val = 128;
        break;

    case SVGA3D_DEVCAP_MAX_TEXTURE_ASPECT_RATIO:
        /* Obsolete */
        break;

    case SVGA3D_DEVCAP_MAX_TEXTURE_ANISOTROPY:
        if (FeatureLevel >= D3D_FEATURE_LEVEL_9_2)
            *pu32Val = D3D11_REQ_MAXANISOTROPY;
        else
            *pu32Val = 2; // D3D_FL9_1_DEFAULT_MAX_ANISOTROPY;
        break;

    case SVGA3D_DEVCAP_MAX_PRIMITIVE_COUNT:
        if (FeatureLevel >= D3D_FEATURE_LEVEL_10_0)
            *pu32Val = UINT32_MAX;
        else if (FeatureLevel >= D3D_FEATURE_LEVEL_9_2)
            *pu32Val = 1048575; // D3D_FL9_2_IA_PRIMITIVE_MAX_COUNT;
        else
            *pu32Val = 65535; // D3D_FL9_1_IA_PRIMITIVE_MAX_COUNT;
        break;

    case SVGA3D_DEVCAP_MAX_VERTEX_INDEX:
        if (FeatureLevel >= D3D_FEATURE_LEVEL_10_0)
            *pu32Val = UINT32_MAX;
        else if (FeatureLevel >= D3D_FEATURE_LEVEL_9_2)
            *pu32Val = 1048575;
        else
            *pu32Val = 65534;
        break;

    case SVGA3D_DEVCAP_MAX_VERTEX_SHADER_INSTRUCTIONS:
        if (FeatureLevel >= D3D_FEATURE_LEVEL_10_0)
            *pu32Val = UINT32_MAX;
        else
            *pu32Val = 512;
        break;

    case SVGA3D_DEVCAP_MAX_FRAGMENT_SHADER_INSTRUCTIONS:
        if (FeatureLevel >= D3D_FEATURE_LEVEL_10_0)
            *pu32Val = UINT32_MAX;
        else
            *pu32Val = 512;
        break;

    case SVGA3D_DEVCAP_MAX_VERTEX_SHADER_TEMPS:
        if (FeatureLevel >= D3D_FEATURE_LEVEL_10_0)
            *pu32Val = 4096;
        else
            *pu32Val = 32;
        break;

    case SVGA3D_DEVCAP_MAX_FRAGMENT_SHADER_TEMPS:
        if (FeatureLevel >= D3D_FEATURE_LEVEL_10_0)
            *pu32Val = 4096;
        else
            *pu32Val = 32;
        break;

    case SVGA3D_DEVCAP_TEXTURE_OPS:
        /* Obsolete */
        break;

    case SVGA3D_DEVCAP_SURFACEFMT_X8R8G8B8:
    case SVGA3D_DEVCAP_SURFACEFMT_A8R8G8B8:
    case SVGA3D_DEVCAP_SURFACEFMT_A2R10G10B10:
    case SVGA3D_DEVCAP_SURFACEFMT_X1R5G5B5:
    case SVGA3D_DEVCAP_SURFACEFMT_A1R5G5B5:
    case SVGA3D_DEVCAP_SURFACEFMT_A4R4G4B4:
    case SVGA3D_DEVCAP_SURFACEFMT_R5G6B5:
    case SVGA3D_DEVCAP_SURFACEFMT_LUMINANCE16:
    case SVGA3D_DEVCAP_SURFACEFMT_LUMINANCE8_ALPHA8:
    case SVGA3D_DEVCAP_SURFACEFMT_ALPHA8:
    case SVGA3D_DEVCAP_SURFACEFMT_LUMINANCE8:
    case SVGA3D_DEVCAP_SURFACEFMT_Z_D16:
    case SVGA3D_DEVCAP_SURFACEFMT_Z_D24S8:
    case SVGA3D_DEVCAP_SURFACEFMT_Z_D24X8:
    case SVGA3D_DEVCAP_SURFACEFMT_DXT1:
    case SVGA3D_DEVCAP_SURFACEFMT_DXT2:
    case SVGA3D_DEVCAP_SURFACEFMT_DXT3:
    case SVGA3D_DEVCAP_SURFACEFMT_DXT4:
    case SVGA3D_DEVCAP_SURFACEFMT_DXT5:
    case SVGA3D_DEVCAP_SURFACEFMT_BUMPX8L8V8U8:
    case SVGA3D_DEVCAP_SURFACEFMT_A2W10V10U10:
    case SVGA3D_DEVCAP_SURFACEFMT_BUMPU8V8:
    case SVGA3D_DEVCAP_SURFACEFMT_Q8W8V8U8:
    case SVGA3D_DEVCAP_SURFACEFMT_CxV8U8:
    case SVGA3D_DEVCAP_SURFACEFMT_R_S10E5:
    case SVGA3D_DEVCAP_SURFACEFMT_R_S23E8:
    case SVGA3D_DEVCAP_SURFACEFMT_RG_S10E5:
    case SVGA3D_DEVCAP_SURFACEFMT_RG_S23E8:
    case SVGA3D_DEVCAP_SURFACEFMT_ARGB_S10E5:
    case SVGA3D_DEVCAP_SURFACEFMT_ARGB_S23E8:
    case SVGA3D_DEVCAP_SURFACEFMT_V16U16:
    case SVGA3D_DEVCAP_SURFACEFMT_G16R16:
    case SVGA3D_DEVCAP_SURFACEFMT_A16B16G16R16:
    case SVGA3D_DEVCAP_SURFACEFMT_UYVY:
    case SVGA3D_DEVCAP_SURFACEFMT_YUY2:
    case SVGA3D_DEVCAP_SURFACEFMT_NV12:
    case SVGA3D_DEVCAP_DEAD10: /* SVGA3D_DEVCAP_SURFACEFMT_AYUV */
    case SVGA3D_DEVCAP_SURFACEFMT_Z_DF16:
    case SVGA3D_DEVCAP_SURFACEFMT_Z_DF24:
    case SVGA3D_DEVCAP_SURFACEFMT_Z_D24S8_INT:
    case SVGA3D_DEVCAP_SURFACEFMT_ATI1:
    case SVGA3D_DEVCAP_SURFACEFMT_ATI2:
    case SVGA3D_DEVCAP_SURFACEFMT_YV12:
    {
        SVGA3dSurfaceFormat const enmFormat = vmsvgaDXDevCapSurfaceFmt2Format(idx3dCaps);
        rc = vmsvgaDXCheckFormatSupportPreDX(pState, enmFormat, pu32Val);
        break;
    }

    case SVGA3D_DEVCAP_MISSING62:
        /* Unused */
        break;

    case SVGA3D_DEVCAP_MAX_VERTEX_SHADER_TEXTURES:
        /* Obsolete */
        break;

    case SVGA3D_DEVCAP_MAX_SIMULTANEOUS_RENDER_TARGETS:
        if (FeatureLevel >= D3D_FEATURE_LEVEL_10_0)
            *pu32Val = 8;
        else if (FeatureLevel >= D3D_FEATURE_LEVEL_9_3)
            *pu32Val = 4; // D3D_FL9_3_SIMULTANEOUS_RENDER_TARGET_COUNT
        else
            *pu32Val = 1; // D3D_FL9_1_SIMULTANEOUS_RENDER_TARGET_COUNT
        break;

    case SVGA3D_DEVCAP_DEAD4: /* SVGA3D_DEVCAP_MULTISAMPLE_NONMASKABLESAMPLES */
    case SVGA3D_DEVCAP_DEAD5: /* SVGA3D_DEVCAP_MULTISAMPLE_MASKABLESAMPLES */
        *pu32Val = (1 << (2-1)) | (1 << (4-1)) | (1 << (8-1)); /* 2x, 4x, 8x */
        break;

    case SVGA3D_DEVCAP_DEAD7: /* SVGA3D_DEVCAP_ALPHATOCOVERAGE */
        /* Obsolete */
        break;

    case SVGA3D_DEVCAP_DEAD6: /* SVGA3D_DEVCAP_SUPERSAMPLE */
        /* Obsolete */
        break;

    case SVGA3D_DEVCAP_AUTOGENMIPMAPS:
        *pu32Val = 1;
        break;

    case SVGA3D_DEVCAP_MAX_CONTEXT_IDS:
        *pu32Val = SVGA3D_MAX_CONTEXT_IDS;
        break;

    case SVGA3D_DEVCAP_MAX_SURFACE_IDS:
        *pu32Val = SVGA3D_MAX_SURFACE_IDS;
        break;

    case SVGA3D_DEVCAP_DEAD1:
        /* Obsolete */
        break;

    case SVGA3D_DEVCAP_DEAD8: /* SVGA3D_DEVCAP_VIDEO_DECODE */
        /* Obsolete */
        break;

    case SVGA3D_DEVCAP_DEAD9: /* SVGA3D_DEVCAP_VIDEO_PROCESS */
        /* Obsolete */
        break;

    case SVGA3D_DEVCAP_LINE_AA:
        *pu32Val = 1;
        break;

    case SVGA3D_DEVCAP_LINE_STIPPLE:
        *pu32Val = 0; /* DX11 does not seem to support this directly. */
        break;

    case SVGA3D_DEVCAP_MAX_LINE_WIDTH:
        AssertCompile(sizeof(uint32_t) == sizeof(float));
        *(float *)pu32Val = 1.0f;
        break;

    case SVGA3D_DEVCAP_MAX_AA_LINE_WIDTH:
        AssertCompile(sizeof(uint32_t) == sizeof(float));
        *(float *)pu32Val = 1.0f;
        break;

    case SVGA3D_DEVCAP_DEAD3: /* Old SVGA3D_DEVCAP_LOGICOPS */
        /* Deprecated. */
        AssertCompile(SVGA3D_DEVCAP_DEAD3 == 92); /* Newer SVGA headers redefine this. */
        break;

    case SVGA3D_DEVCAP_TS_COLOR_KEY:
        *pu32Val = 0; /* DX11 does not seem to support this directly. */
        break;

    case SVGA3D_DEVCAP_DEAD2:
        break;

    case SVGA3D_DEVCAP_DXCONTEXT:
        *pu32Val = 1;
        break;

    case SVGA3D_DEVCAP_DEAD11: /* SVGA3D_DEVCAP_MAX_TEXTURE_ARRAY_SIZE */
        *pu32Val = D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
        break;

    case SVGA3D_DEVCAP_DX_MAX_VERTEXBUFFERS:
        *pu32Val = D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
        break;

    case SVGA3D_DEVCAP_DX_MAX_CONSTANT_BUFFERS:
        *pu32Val = D3D11_COMMONSHADER_CONSTANT_BUFFER_HW_SLOT_COUNT;
        break;

    case SVGA3D_DEVCAP_DX_PROVOKING_VERTEX:
        *pu32Val = 0; /* boolean */
        break;

    case SVGA3D_DEVCAP_DXFMT_X8R8G8B8:
    case SVGA3D_DEVCAP_DXFMT_A8R8G8B8:
    case SVGA3D_DEVCAP_DXFMT_R5G6B5:
    case SVGA3D_DEVCAP_DXFMT_X1R5G5B5:
    case SVGA3D_DEVCAP_DXFMT_A1R5G5B5:
    case SVGA3D_DEVCAP_DXFMT_A4R4G4B4:
    case SVGA3D_DEVCAP_DXFMT_Z_D32:
    case SVGA3D_DEVCAP_DXFMT_Z_D16:
    case SVGA3D_DEVCAP_DXFMT_Z_D24S8:
    case SVGA3D_DEVCAP_DXFMT_Z_D15S1:
    case SVGA3D_DEVCAP_DXFMT_LUMINANCE8:
    case SVGA3D_DEVCAP_DXFMT_LUMINANCE4_ALPHA4:
    case SVGA3D_DEVCAP_DXFMT_LUMINANCE16:
    case SVGA3D_DEVCAP_DXFMT_LUMINANCE8_ALPHA8:
    case SVGA3D_DEVCAP_DXFMT_DXT1:
    case SVGA3D_DEVCAP_DXFMT_DXT2:
    case SVGA3D_DEVCAP_DXFMT_DXT3:
    case SVGA3D_DEVCAP_DXFMT_DXT4:
    case SVGA3D_DEVCAP_DXFMT_DXT5:
    case SVGA3D_DEVCAP_DXFMT_BUMPU8V8:
    case SVGA3D_DEVCAP_DXFMT_BUMPL6V5U5:
    case SVGA3D_DEVCAP_DXFMT_BUMPX8L8V8U8:
    case SVGA3D_DEVCAP_DXFMT_FORMAT_DEAD1:
    case SVGA3D_DEVCAP_DXFMT_ARGB_S10E5:
    case SVGA3D_DEVCAP_DXFMT_ARGB_S23E8:
    case SVGA3D_DEVCAP_DXFMT_A2R10G10B10:
    case SVGA3D_DEVCAP_DXFMT_V8U8:
    case SVGA3D_DEVCAP_DXFMT_Q8W8V8U8:
    case SVGA3D_DEVCAP_DXFMT_CxV8U8:
    case SVGA3D_DEVCAP_DXFMT_X8L8V8U8:
    case SVGA3D_DEVCAP_DXFMT_A2W10V10U10:
    case SVGA3D_DEVCAP_DXFMT_ALPHA8:
    case SVGA3D_DEVCAP_DXFMT_R_S10E5:
    case SVGA3D_DEVCAP_DXFMT_R_S23E8:
    case SVGA3D_DEVCAP_DXFMT_RG_S10E5:
    case SVGA3D_DEVCAP_DXFMT_RG_S23E8:
    case SVGA3D_DEVCAP_DXFMT_BUFFER:
    case SVGA3D_DEVCAP_DXFMT_Z_D24X8:
    case SVGA3D_DEVCAP_DXFMT_V16U16:
    case SVGA3D_DEVCAP_DXFMT_G16R16:
    case SVGA3D_DEVCAP_DXFMT_A16B16G16R16:
    case SVGA3D_DEVCAP_DXFMT_UYVY:
    case SVGA3D_DEVCAP_DXFMT_YUY2:
    case SVGA3D_DEVCAP_DXFMT_NV12:
    case SVGA3D_DEVCAP_DXFMT_FORMAT_DEAD2: /* SVGA3D_DEVCAP_DXFMT_AYUV */
    case SVGA3D_DEVCAP_DXFMT_R32G32B32A32_TYPELESS:
    case SVGA3D_DEVCAP_DXFMT_R32G32B32A32_UINT:
    case SVGA3D_DEVCAP_DXFMT_R32G32B32A32_SINT:
    case SVGA3D_DEVCAP_DXFMT_R32G32B32_TYPELESS:
    case SVGA3D_DEVCAP_DXFMT_R32G32B32_FLOAT:
    case SVGA3D_DEVCAP_DXFMT_R32G32B32_UINT:
    case SVGA3D_DEVCAP_DXFMT_R32G32B32_SINT:
    case SVGA3D_DEVCAP_DXFMT_R16G16B16A16_TYPELESS:
    case SVGA3D_DEVCAP_DXFMT_R16G16B16A16_UINT:
    case SVGA3D_DEVCAP_DXFMT_R16G16B16A16_SNORM:
    case SVGA3D_DEVCAP_DXFMT_R16G16B16A16_SINT:
    case SVGA3D_DEVCAP_DXFMT_R32G32_TYPELESS:
    case SVGA3D_DEVCAP_DXFMT_R32G32_UINT:
    case SVGA3D_DEVCAP_DXFMT_R32G32_SINT:
    case SVGA3D_DEVCAP_DXFMT_R32G8X24_TYPELESS:
    case SVGA3D_DEVCAP_DXFMT_D32_FLOAT_S8X24_UINT:
    case SVGA3D_DEVCAP_DXFMT_R32_FLOAT_X8X24:
    case SVGA3D_DEVCAP_DXFMT_X32_G8X24_UINT:
    case SVGA3D_DEVCAP_DXFMT_R10G10B10A2_TYPELESS:
    case SVGA3D_DEVCAP_DXFMT_R10G10B10A2_UINT:
    case SVGA3D_DEVCAP_DXFMT_R11G11B10_FLOAT:
    case SVGA3D_DEVCAP_DXFMT_R8G8B8A8_TYPELESS:
    case SVGA3D_DEVCAP_DXFMT_R8G8B8A8_UNORM:
    case SVGA3D_DEVCAP_DXFMT_R8G8B8A8_UNORM_SRGB:
    case SVGA3D_DEVCAP_DXFMT_R8G8B8A8_UINT:
    case SVGA3D_DEVCAP_DXFMT_R8G8B8A8_SINT:
    case SVGA3D_DEVCAP_DXFMT_R16G16_TYPELESS:
    case SVGA3D_DEVCAP_DXFMT_R16G16_UINT:
    case SVGA3D_DEVCAP_DXFMT_R16G16_SINT:
    case SVGA3D_DEVCAP_DXFMT_R32_TYPELESS:
    case SVGA3D_DEVCAP_DXFMT_D32_FLOAT:
    case SVGA3D_DEVCAP_DXFMT_R32_UINT:
    case SVGA3D_DEVCAP_DXFMT_R32_SINT:
    case SVGA3D_DEVCAP_DXFMT_R24G8_TYPELESS:
    case SVGA3D_DEVCAP_DXFMT_D24_UNORM_S8_UINT:
    case SVGA3D_DEVCAP_DXFMT_R24_UNORM_X8:
    case SVGA3D_DEVCAP_DXFMT_X24_G8_UINT:
    case SVGA3D_DEVCAP_DXFMT_R8G8_TYPELESS:
    case SVGA3D_DEVCAP_DXFMT_R8G8_UNORM:
    case SVGA3D_DEVCAP_DXFMT_R8G8_UINT:
    case SVGA3D_DEVCAP_DXFMT_R8G8_SINT:
    case SVGA3D_DEVCAP_DXFMT_R16_TYPELESS:
    case SVGA3D_DEVCAP_DXFMT_R16_UNORM:
    case SVGA3D_DEVCAP_DXFMT_R16_UINT:
    case SVGA3D_DEVCAP_DXFMT_R16_SNORM:
    case SVGA3D_DEVCAP_DXFMT_R16_SINT:
    case SVGA3D_DEVCAP_DXFMT_R8_TYPELESS:
    case SVGA3D_DEVCAP_DXFMT_R8_UNORM:
    case SVGA3D_DEVCAP_DXFMT_R8_UINT:
    case SVGA3D_DEVCAP_DXFMT_R8_SNORM:
    case SVGA3D_DEVCAP_DXFMT_R8_SINT:
    case SVGA3D_DEVCAP_DXFMT_P8:
    case SVGA3D_DEVCAP_DXFMT_R9G9B9E5_SHAREDEXP:
    case SVGA3D_DEVCAP_DXFMT_R8G8_B8G8_UNORM:
    case SVGA3D_DEVCAP_DXFMT_G8R8_G8B8_UNORM:
    case SVGA3D_DEVCAP_DXFMT_BC1_TYPELESS:
    case SVGA3D_DEVCAP_DXFMT_BC1_UNORM_SRGB:
    case SVGA3D_DEVCAP_DXFMT_BC2_TYPELESS:
    case SVGA3D_DEVCAP_DXFMT_BC2_UNORM_SRGB:
    case SVGA3D_DEVCAP_DXFMT_BC3_TYPELESS:
    case SVGA3D_DEVCAP_DXFMT_BC3_UNORM_SRGB:
    case SVGA3D_DEVCAP_DXFMT_BC4_TYPELESS:
    case SVGA3D_DEVCAP_DXFMT_ATI1:
    case SVGA3D_DEVCAP_DXFMT_BC4_SNORM:
    case SVGA3D_DEVCAP_DXFMT_BC5_TYPELESS:
    case SVGA3D_DEVCAP_DXFMT_ATI2:
    case SVGA3D_DEVCAP_DXFMT_BC5_SNORM:
    case SVGA3D_DEVCAP_DXFMT_R10G10B10_XR_BIAS_A2_UNORM:
    case SVGA3D_DEVCAP_DXFMT_B8G8R8A8_TYPELESS:
    case SVGA3D_DEVCAP_DXFMT_B8G8R8A8_UNORM_SRGB:
    case SVGA3D_DEVCAP_DXFMT_B8G8R8X8_TYPELESS:
    case SVGA3D_DEVCAP_DXFMT_B8G8R8X8_UNORM_SRGB:
    case SVGA3D_DEVCAP_DXFMT_Z_DF16:
    case SVGA3D_DEVCAP_DXFMT_Z_DF24:
    case SVGA3D_DEVCAP_DXFMT_Z_D24S8_INT:
    case SVGA3D_DEVCAP_DXFMT_YV12:
    case SVGA3D_DEVCAP_DXFMT_R32G32B32A32_FLOAT:
    case SVGA3D_DEVCAP_DXFMT_R16G16B16A16_FLOAT:
    case SVGA3D_DEVCAP_DXFMT_R16G16B16A16_UNORM:
    case SVGA3D_DEVCAP_DXFMT_R32G32_FLOAT:
    case SVGA3D_DEVCAP_DXFMT_R10G10B10A2_UNORM:
    case SVGA3D_DEVCAP_DXFMT_R8G8B8A8_SNORM:
    case SVGA3D_DEVCAP_DXFMT_R16G16_FLOAT:
    case SVGA3D_DEVCAP_DXFMT_R16G16_UNORM:
    case SVGA3D_DEVCAP_DXFMT_R16G16_SNORM:
    case SVGA3D_DEVCAP_DXFMT_R32_FLOAT:
    case SVGA3D_DEVCAP_DXFMT_R8G8_SNORM:
    case SVGA3D_DEVCAP_DXFMT_R16_FLOAT:
    case SVGA3D_DEVCAP_DXFMT_D16_UNORM:
    case SVGA3D_DEVCAP_DXFMT_A8_UNORM:
    case SVGA3D_DEVCAP_DXFMT_BC1_UNORM:
    case SVGA3D_DEVCAP_DXFMT_BC2_UNORM:
    case SVGA3D_DEVCAP_DXFMT_BC3_UNORM:
    case SVGA3D_DEVCAP_DXFMT_B5G6R5_UNORM:
    case SVGA3D_DEVCAP_DXFMT_B5G5R5A1_UNORM:
    case SVGA3D_DEVCAP_DXFMT_B8G8R8A8_UNORM:
    case SVGA3D_DEVCAP_DXFMT_B8G8R8X8_UNORM:
    case SVGA3D_DEVCAP_DXFMT_BC4_UNORM:
    case SVGA3D_DEVCAP_DXFMT_BC5_UNORM:
    case SVGA3D_DEVCAP_DXFMT_BC6H_TYPELESS:
    case SVGA3D_DEVCAP_DXFMT_BC6H_UF16:
    case SVGA3D_DEVCAP_DXFMT_BC6H_SF16:
    case SVGA3D_DEVCAP_DXFMT_BC7_TYPELESS:
    case SVGA3D_DEVCAP_DXFMT_BC7_UNORM:
    case SVGA3D_DEVCAP_DXFMT_BC7_UNORM_SRGB:
    {
        SVGA3dSurfaceFormat const enmFormat = vmsvgaDXDevCapDxfmt2Format(idx3dCaps);
        rc = vmsvgaDXCheckFormatSupport(pState, enmFormat, pu32Val);
        break;
    }

    case SVGA3D_DEVCAP_SM41:
        *pu32Val = 1; /* boolean */
        break;

    case SVGA3D_DEVCAP_MULTISAMPLE_2X:
        *pu32Val = RT_BOOL(pState->pBackend->dxDevice.MultisampleCountMask & (1 << (2 - 1))); /* boolean */
        break;

    case SVGA3D_DEVCAP_MULTISAMPLE_4X:
        *pu32Val = RT_BOOL(pState->pBackend->dxDevice.MultisampleCountMask & (1 << (4 - 1))); /* boolean */
        break;

    case SVGA3D_DEVCAP_MS_FULL_QUALITY:
        *pu32Val = 0; /* boolean */
        break;

    case SVGA3D_DEVCAP_LOGICOPS:
        AssertCompile(SVGA3D_DEVCAP_LOGICOPS == 248);
        *pu32Val = 0; /* boolean */
        break;

    case SVGA3D_DEVCAP_LOGIC_BLENDOPS:
        *pu32Val = 0; /* boolean */
        break;

    case SVGA3D_DEVCAP_RESERVED_1:
        break;

    case SVGA3D_DEVCAP_RESERVED_2:
        break;

    case SVGA3D_DEVCAP_SM5:
        *pu32Val = 1; /* boolean */
        break;

    case SVGA3D_DEVCAP_MULTISAMPLE_8X:
        *pu32Val = RT_BOOL(pState->pBackend->dxDevice.MultisampleCountMask & (1 << (8 - 1))); /* boolean */
        break;

    case SVGA3D_DEVCAP_MAX:
    case SVGA3D_DEVCAP_INVALID:
        rc = VERR_NOT_SUPPORTED;
        break;
    }

    return rc;
}


static DECLCALLBACK(int) vmsvga3dBackChangeMode(PVGASTATECC pThisCC)
{
    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackSurfaceCopy(PVGASTATECC pThisCC, SVGA3dSurfaceImageId dest, SVGA3dSurfaceImageId src,
                               uint32_t cCopyBoxes, SVGA3dCopyBox *pBox)
{
    RT_NOREF(cCopyBoxes);
    AssertReturn(pBox, VERR_INVALID_PARAMETER);

    LogFunc(("src sid %d -> dst sid %d\n", src.sid, dest.sid));

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    PVMSVGA3DBACKEND pBackend = pState->pBackend;

    PVMSVGA3DSURFACE pSrcSurface;
    int rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, src.sid, &pSrcSurface);
    AssertRCReturn(rc, rc);

    PVMSVGA3DSURFACE pDstSurface;
    rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, dest.sid, &pDstSurface);
    AssertRCReturn(rc, rc);

/** @todo Implement a separate code paths for memory->texture, texture->memory */
    LogFunc(("src%s sid = %u -> dst%s sid = %u\n",
             pSrcSurface->pBackendSurface ? "" : " sysmem", pSrcSurface->id,
             pDstSurface->pBackendSurface ? "" : " sysmem", pDstSurface->id));

    /* Clip the box. */
    PVMSVGA3DMIPMAPLEVEL pSrcMipLevel;
    rc = vmsvga3dMipmapLevel(pSrcSurface, src.face, src.mipmap, &pSrcMipLevel);
    ASSERT_GUEST_RETURN(RT_SUCCESS(rc), rc);

    PVMSVGA3DMIPMAPLEVEL pDstMipLevel;
    rc = vmsvga3dMipmapLevel(pDstSurface, dest.face, dest.mipmap, &pDstMipLevel);
    ASSERT_GUEST_RETURN(RT_SUCCESS(rc), rc);

    SVGA3dCopyBox clipBox = *pBox;
    vmsvgaR3ClipCopyBox(&pSrcMipLevel->mipmapSize, &pDstMipLevel->mipmapSize, &clipBox);

    if (pSrcSurface->pBackendSurface == NULL && pDstSurface->pBackendSurface == NULL)
    {
        AssertReturn(pSrcSurface->format == pDstSurface->format, VERR_INVALID_PARAMETER);
        AssertReturn(pSrcSurface->cbBlock == pDstSurface->cbBlock, VERR_INVALID_PARAMETER);
        AssertReturn(pSrcMipLevel->pSurfaceData && pDstMipLevel->pSurfaceData, VERR_INVALID_STATE);

        uint32_t const cxBlocks = (clipBox.w + pSrcSurface->cxBlock - 1) / pSrcSurface->cxBlock;
        uint32_t const cyBlocks = (clipBox.h + pSrcSurface->cyBlock - 1) / pSrcSurface->cyBlock;
        uint32_t const cbRow = cxBlocks * pSrcSurface->cbBlock;

        uint8_t const *pu8Src = (uint8_t *)pSrcMipLevel->pSurfaceData
                + (clipBox.srcx / pSrcSurface->cxBlock) * pSrcSurface->cbBlock
                + (clipBox.srcy / pSrcSurface->cyBlock) * pSrcMipLevel->cbSurfacePitch
                + clipBox.srcz * pSrcMipLevel->cbSurfacePlane;

        uint8_t *pu8Dst = (uint8_t *)pDstMipLevel->pSurfaceData
                + (clipBox.x / pDstSurface->cxBlock) * pDstSurface->cbBlock
                + (clipBox.y / pDstSurface->cyBlock) * pDstMipLevel->cbSurfacePitch
                + clipBox.z * pDstMipLevel->cbSurfacePlane;

        for (uint32_t z = 0; z < clipBox.d; ++z)
        {
            uint8_t const *pu8PlaneSrc = pu8Src;
            uint8_t *pu8PlaneDst = pu8Dst;

            for (uint32_t y = 0; y < cyBlocks; ++y)
            {
                memcpy(pu8PlaneDst, pu8PlaneSrc, cbRow);
                pu8PlaneDst += pDstMipLevel->cbSurfacePitch;
                pu8PlaneSrc += pSrcMipLevel->cbSurfacePitch;
            }

            pu8Src += pSrcMipLevel->cbSurfacePlane;
            pu8Dst += pDstMipLevel->cbSurfacePlane;
        }

        return VINF_SUCCESS;
    }

    //DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    //AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);
    DXDEVICE *pDXDevice = &pBackend->dxDevice;

    if (pSrcSurface->pBackendSurface == NULL)
    {
        rc = vmsvga3dBackSurfaceCreateResource(pThisCC, pSrcSurface);
        AssertRCReturn(rc, rc);
    }

    if (pDstSurface->pBackendSurface == NULL)
    {
        rc = vmsvga3dBackSurfaceCreateResource(pThisCC, pDstSurface);
        AssertRCReturn(rc, rc);
    }

    UINT DstSubresource = vmsvga3dCalcSubresource(dest.mipmap, dest.face, pDstSurface->cLevels);
    UINT DstX = clipBox.x;
    UINT DstY = clipBox.y;
    UINT DstZ = clipBox.z;

    UINT SrcSubresource = vmsvga3dCalcSubresource(src.mipmap, src.face, pSrcSurface->cLevels);
    D3D11_BOX SrcBox;
    SrcBox.left   = clipBox.srcx;
    SrcBox.top    = clipBox.srcy;
    SrcBox.front  = clipBox.srcz;
    SrcBox.right  = clipBox.srcx + clipBox.w;
    SrcBox.bottom = clipBox.srcy + clipBox.h;
    SrcBox.back   = clipBox.srcz + clipBox.d;

    Assert(cCopyBoxes == 1); /** @todo */

    ID3D11Resource *pDstResource;
    ID3D11Resource *pSrcResource;
    pDstResource = dxResource(pDstSurface);
    pSrcResource = dxResource(pSrcSurface);

    pDXDevice->pImmediateContext->CopySubresourceRegion(pDstResource, DstSubresource, DstX, DstY, DstZ,
                                                        pSrcResource, SrcSubresource, &SrcBox);

    return rc;
}


static DECLCALLBACK(void) vmsvga3dBackUpdateHostScreenViewport(PVGASTATECC pThisCC, uint32_t idScreen, VMSVGAVIEWPORT const *pOldViewport)
{
    RT_NOREF(pThisCC, idScreen, pOldViewport);
    /** @todo Scroll the screen content without requiring the guest to redraw. */
}


static DECLCALLBACK(int) vmsvga3dBackSurfaceUpdateHeapBuffers(PVGASTATECC pThisCC, PVMSVGA3DSURFACE pSurface)
{
    /** @todo */
    RT_NOREF(pThisCC, pSurface);
    return VERR_NOT_IMPLEMENTED;
}


/*
 *
 * VGPU9 callbacks. Not implemented.
 *
 */
/** @todo later */

/**
 * Create a new 3d context
 *
 * @returns VBox status code.
 * @param   pThisCC         The VGA/VMSVGA state for ring-3.
 * @param   cid             Context id
 */
static DECLCALLBACK(int) vmsvga3dBackContextDefine(PVGASTATECC pThisCC, uint32_t cid)
{
    RT_NOREF(cid);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    DEBUG_BREAKPOINT_TEST();
    return VERR_NOT_IMPLEMENTED;
}


/**
 * Destroy an existing 3d context
 *
 * @returns VBox status code.
 * @param   pThisCC         The VGA/VMSVGA state for ring-3.
 * @param   cid             Context id
 */
static DECLCALLBACK(int) vmsvga3dBackContextDestroy(PVGASTATECC pThisCC, uint32_t cid)
{
    RT_NOREF(cid);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackSetTransform(PVGASTATECC pThisCC, uint32_t cid, SVGA3dTransformType type, float matrix[16])
{
    RT_NOREF(cid, type, matrix);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackSetZRange(PVGASTATECC pThisCC, uint32_t cid, SVGA3dZRange zRange)
{
    RT_NOREF(cid, zRange);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackSetRenderState(PVGASTATECC pThisCC, uint32_t cid, uint32_t cRenderStates, SVGA3dRenderState *pRenderState)
{
    RT_NOREF(cid, cRenderStates, pRenderState);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackSetRenderTarget(PVGASTATECC pThisCC, uint32_t cid, SVGA3dRenderTargetType type, SVGA3dSurfaceImageId target)
{
    RT_NOREF(cid, type, target);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackSetTextureState(PVGASTATECC pThisCC, uint32_t cid, uint32_t cTextureStates, SVGA3dTextureState *pTextureState)
{
    RT_NOREF(cid, cTextureStates, pTextureState);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackSetMaterial(PVGASTATECC pThisCC, uint32_t cid, SVGA3dFace face, SVGA3dMaterial *pMaterial)
{
    RT_NOREF(cid, face, pMaterial);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackSetLightData(PVGASTATECC pThisCC, uint32_t cid, uint32_t index, SVGA3dLightData *pData)
{
    RT_NOREF(cid, index, pData);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackSetLightEnabled(PVGASTATECC pThisCC, uint32_t cid, uint32_t index, uint32_t enabled)
{
    RT_NOREF(cid, index, enabled);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackSetViewPort(PVGASTATECC pThisCC, uint32_t cid, SVGA3dRect *pRect)
{
    RT_NOREF(cid, pRect);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackSetClipPlane(PVGASTATECC pThisCC, uint32_t cid, uint32_t index, float plane[4])
{
    RT_NOREF(cid, index, plane);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackCommandClear(PVGASTATECC pThisCC, uint32_t cid, SVGA3dClearFlag clearFlag, uint32_t color, float depth,
                                    uint32_t stencil, uint32_t cRects, SVGA3dRect *pRect)
{
    /* From SVGA3D_BeginClear comments:
     *
     *      Clear is not affected by clipping, depth test, or other
     *      render state which affects the fragment pipeline.
     *
     * Therefore this code must ignore the current scissor rect.
     */

    RT_NOREF(cid, clearFlag, color, depth, stencil, cRects, pRect);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDrawPrimitives(PVGASTATECC pThisCC, uint32_t cid, uint32_t numVertexDecls, SVGA3dVertexDecl *pVertexDecl,
                                  uint32_t numRanges, SVGA3dPrimitiveRange *pRange,
                                  uint32_t cVertexDivisor, SVGA3dVertexDivisor *pVertexDivisor)
{
    RT_NOREF(cid, numVertexDecls, pVertexDecl, numRanges, pRange, cVertexDivisor, pVertexDivisor);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackSetScissorRect(PVGASTATECC pThisCC, uint32_t cid, SVGA3dRect *pRect)
{
    RT_NOREF(cid, pRect);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackGenerateMipmaps(PVGASTATECC pThisCC, uint32_t sid, SVGA3dTextureFilter filter)
{
    RT_NOREF(sid, filter);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackShaderDefine(PVGASTATECC pThisCC, uint32_t cid, uint32_t shid, SVGA3dShaderType type,
                                uint32_t cbData, uint32_t *pShaderData)
{
    RT_NOREF(cid, shid, type, cbData, pShaderData);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackShaderDestroy(PVGASTATECC pThisCC, uint32_t cid, uint32_t shid, SVGA3dShaderType type)
{
    RT_NOREF(cid, shid, type);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackShaderSet(PVGASTATECC pThisCC, PVMSVGA3DCONTEXT pContext, uint32_t cid, SVGA3dShaderType type, uint32_t shid)
{
    RT_NOREF(pContext, cid, type, shid);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackShaderSetConst(PVGASTATECC pThisCC, uint32_t cid, uint32_t reg, SVGA3dShaderType type,
                                  SVGA3dShaderConstType ctype, uint32_t cRegisters, uint32_t *pValues)
{
    RT_NOREF(cid, reg, type, ctype, cRegisters, pValues);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) vmsvga3dBackOcclusionQueryCreate(PVGASTATECC pThisCC, PVMSVGA3DCONTEXT pContext)
{
    RT_NOREF(pThisCC, pContext);
    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) vmsvga3dBackOcclusionQueryDelete(PVGASTATECC pThisCC, PVMSVGA3DCONTEXT pContext)
{
    RT_NOREF(pThisCC, pContext);
    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) vmsvga3dBackOcclusionQueryBegin(PVGASTATECC pThisCC, PVMSVGA3DCONTEXT pContext)
{
    RT_NOREF(pThisCC, pContext);
    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) vmsvga3dBackOcclusionQueryEnd(PVGASTATECC pThisCC, PVMSVGA3DCONTEXT pContext)
{
    RT_NOREF(pThisCC, pContext);
    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) vmsvga3dBackOcclusionQueryGetData(PVGASTATECC pThisCC, PVMSVGA3DCONTEXT pContext, uint32_t *pu32Pixels)
{
    RT_NOREF(pThisCC, pContext, pu32Pixels);
    DEBUG_BREAKPOINT_TEST();
    return VINF_SUCCESS;
}


/**
 * Destroy backend specific surface bits (part of SVGA_3D_CMD_SURFACE_DESTROY).
 *
 * @param   pThisCC             The device context.
 * @param   fClearCOTableEntry  Whether to clear the corresponding COTable entry.
 * @param   pSurface            The surface being destroyed.
 */
static DECLCALLBACK(void) vmsvga3dBackSurfaceDestroy(PVGASTATECC pThisCC, bool fClearCOTableEntry, PVMSVGA3DSURFACE pSurface)
{
    RT_NOREF(pThisCC);

    /* The caller should not use the function for system memory surfaces. */
    PVMSVGA3DBACKENDSURFACE pBackendSurface = pSurface->pBackendSurface;
    if (!pBackendSurface)
        return;
    pSurface->pBackendSurface = NULL;

    LogFunc(("sid=%u\n", pSurface->id));

    /* If any views have been created for this resource, then also release them. */
    DXVIEW *pIter, *pNext;
    RTListForEachSafe(&pBackendSurface->listView, pIter, pNext, DXVIEW, nodeSurfaceView)
    {
        LogFunc(("pIter=%p, pNext=%p\n", pIter, pNext));

        /** @todo The common DX code should track the views and clean COTable on a surface destruction. */
        if (fClearCOTableEntry)
        {
            PVMSVGA3DDXCONTEXT pDXContext;
            int rc = vmsvga3dDXContextFromCid(pThisCC->svga.p3dState, pIter->cid, &pDXContext);
            AssertRC(rc);
            if (RT_SUCCESS(rc))
            {
                switch (pIter->enmViewType)
                {
                    case VMSVGA3D_VIEWTYPE_RENDERTARGET:
                    {
                        SVGACOTableDXRTViewEntry *pEntry = &pDXContext->cot.paRTView[pIter->viewId];
                        RT_ZERO(*pEntry);
                        break;
                    }
                    case VMSVGA3D_VIEWTYPE_DEPTHSTENCIL:
                    {
                        SVGACOTableDXDSViewEntry *pEntry = &pDXContext->cot.paDSView[pIter->viewId];
                        RT_ZERO(*pEntry);
                        break;
                    }
                    case VMSVGA3D_VIEWTYPE_SHADERRESOURCE:
                    {
                        SVGACOTableDXSRViewEntry *pEntry = &pDXContext->cot.paSRView[pIter->viewId];
                        RT_ZERO(*pEntry);
                        break;
                    }
                    case VMSVGA3D_VIEWTYPE_UNORDEREDACCESS:
                    {
                        SVGACOTableDXUAViewEntry *pEntry = &pDXContext->cot.paUAView[pIter->viewId];
                        RT_ZERO(*pEntry);
                        break;
                    }
                    case VMSVGA3D_VIEWTYPE_VIDEODECODEROUTPUT:
                    {
                        VBSVGACOTableDXVideoDecoderOutputViewEntry *pEntry = &pDXContext->cot.paVideoDecoderOutputView[pIter->viewId];
                        RT_ZERO(*pEntry);
                        break;
                    }
                    case VMSVGA3D_VIEWTYPE_VIDEOPROCESSORINPUT:
                    {
                        VBSVGACOTableDXVideoProcessorInputViewEntry *pEntry = &pDXContext->cot.paVideoProcessorInputView[pIter->viewId];
                        RT_ZERO(*pEntry);
                        break;
                    }
                    case VMSVGA3D_VIEWTYPE_VIDEOPROCESSOROUTPUT:
                    {
                        VBSVGACOTableDXVideoProcessorOutputViewEntry *pEntry = &pDXContext->cot.paVideoProcessorOutputView[pIter->viewId];
                        RT_ZERO(*pEntry);
                        break;
                    }
                    case VMSVGA3D_VIEWTYPE_NONE:
                        AssertFailed();
                        break;
                }
            }
        }

        dxViewDestroy(pIter);
    }

    if (   pBackendSurface->enmResType == VMSVGA3D_RESTYPE_TEXTURE_1D
        || pBackendSurface->enmResType == VMSVGA3D_RESTYPE_TEXTURE_2D
        || pBackendSurface->enmResType == VMSVGA3D_RESTYPE_TEXTURE_CUBE
        || pBackendSurface->enmResType == VMSVGA3D_RESTYPE_TEXTURE_3D)
    {
        D3D_RELEASE(pBackendSurface->staging.pResource);
        D3D_RELEASE(pBackendSurface->dynamic.pResource);
        D3D_RELEASE(pBackendSurface->u.pResource);
    }
    else if (pBackendSurface->enmResType == VMSVGA3D_RESTYPE_BUFFER)
    {
        D3D_RELEASE(pBackendSurface->staging.pBuffer);
        D3D_RELEASE(pBackendSurface->dynamic.pBuffer);
        D3D_RELEASE(pBackendSurface->u.pBuffer);
    }
    else
    {
        AssertFailed();
    }

    RTMemFree(pBackendSurface);
}


static DECLCALLBACK(void) vmsvga3dBackSurfaceInvalidateImage(PVGASTATECC pThisCC, PVMSVGA3DSURFACE pSurface, uint32_t uFace, uint32_t uMipmap)
{
    RT_NOREF(pThisCC, uFace, uMipmap);

    /* The caller should not use the function for system memory surfaces. */
    PVMSVGA3DBACKENDSURFACE pBackendSurface = pSurface->pBackendSurface;
    if (!pBackendSurface)
        return;

    LogFunc(("sid=%u\n", pSurface->id));

    /* The guest uses this to invalidate a buffer. */
    if (pBackendSurface->enmResType == VMSVGA3D_RESTYPE_BUFFER)
    {
        Assert(uFace == 0 && uMipmap == 0); /* The caller ensures this. */
        /** @todo This causes flickering when a buffer is invalidated and re-created right before a draw call. */
        //vmsvga3dBackSurfaceDestroy(pThisCC, false, pSurface);
    }
    else
    {
        /** @todo Delete views that have been created for this mipmap.
         * For now just delete all views, they will be recte=reated if necessary.
         */
        ASSERT_GUEST_FAILED();
        DXVIEW *pIter, *pNext;
        RTListForEachSafe(&pBackendSurface->listView, pIter, pNext, DXVIEW, nodeSurfaceView)
        {
            dxViewDestroy(pIter);
        }
    }
}


/**
 * Backend worker for implementing SVGA_3D_CMD_SURFACE_STRETCHBLT.
 *
 * @returns VBox status code.
 * @param   pThis               The VGA device instance.
 * @param   pState              The VMSVGA3d state.
 * @param   pDstSurface         The destination host surface.
 * @param   uDstFace            The destination face (valid).
 * @param   uDstMipmap          The destination mipmap level (valid).
 * @param   pDstBox             The destination box.
 * @param   pSrcSurface         The source host surface.
 * @param   uSrcFace            The destination face (valid).
 * @param   uSrcMipmap          The source mimap level (valid).
 * @param   pSrcBox             The source box.
 * @param   enmMode             The strecht blt mode .
 * @param   pContext            The VMSVGA3d context (already current for OGL).
 */
static DECLCALLBACK(int) vmsvga3dBackSurfaceStretchBlt(PVGASTATE pThis, PVMSVGA3DSTATE pState,
                                  PVMSVGA3DSURFACE pDstSurface, uint32_t uDstFace, uint32_t uDstMipmap, SVGA3dBox const *pDstBox,
                                  PVMSVGA3DSURFACE pSrcSurface, uint32_t uSrcFace, uint32_t uSrcMipmap, SVGA3dBox const *pSrcBox,
                                  SVGA3dStretchBltMode enmMode, PVMSVGA3DCONTEXT pContext)
{
    RT_NOREF(pThis, pState, pDstSurface, uDstFace, uDstMipmap, pDstBox,
             pSrcSurface, uSrcFace, uSrcMipmap, pSrcBox, enmMode, pContext);

    AssertFailed();
    return VINF_SUCCESS;
}


/**
 * Backend worker for implementing SVGA_3D_CMD_SURFACE_DMA that copies one box.
 *
 * @returns Failure status code or @a rc.
 * @param   pThis               The shared VGA/VMSVGA instance data.
 * @param   pThisCC             The VGA/VMSVGA state for ring-3.
 * @param   pState              The VMSVGA3d state.
 * @param   pSurface            The host surface.
 * @param   pMipLevel           Mipmap level. The caller knows it already.
 * @param   uHostFace           The host face (valid).
 * @param   uHostMipmap         The host mipmap level (valid).
 * @param   GuestPtr            The guest pointer.
 * @param   cbGuestPitch        The guest pitch.
 * @param   transfer            The transfer direction.
 * @param   pBox                The box to copy (clipped, valid, except for guest's srcx, srcy, srcz).
 * @param   pContext            The context (for OpenGL).
 * @param   rc                  The current rc for all boxes.
 * @param   iBox                The current box number (for Direct 3D).
 */
static DECLCALLBACK(int) vmsvga3dBackSurfaceDMACopyBox(PVGASTATE pThis, PVGASTATECC pThisCC, PVMSVGA3DSTATE pState, PVMSVGA3DSURFACE pSurface,
                                  PVMSVGA3DMIPMAPLEVEL pMipLevel, uint32_t uHostFace, uint32_t uHostMipmap,
                                  SVGAGuestPtr GuestPtr, uint32_t cbGuestPitch, SVGA3dTransferType transfer,
                                  SVGA3dCopyBox const *pBox, PVMSVGA3DCONTEXT pContext, int rc, int iBox)
{
    RT_NOREF(pState, pMipLevel, pContext, iBox);

    /* The called should not use the function for system memory surfaces. */
    PVMSVGA3DBACKENDSURFACE pBackendSurface = pSurface->pBackendSurface;
    AssertReturn(pBackendSurface, VERR_INVALID_PARAMETER);

    if (   pBackendSurface->enmResType == VMSVGA3D_RESTYPE_TEXTURE_1D
        || pBackendSurface->enmResType == VMSVGA3D_RESTYPE_TEXTURE_2D
        || pBackendSurface->enmResType == VMSVGA3D_RESTYPE_TEXTURE_CUBE
        || pBackendSurface->enmResType == VMSVGA3D_RESTYPE_TEXTURE_3D)
    {
        /** @todo This is generic code and should be in DevVGA-SVGA3d.cpp for backends which support Map/Unmap. */
        /** @todo Use vmsvga3dGetBoxDimensions because it does clipping and calculations. */
        uint32_t const u32GuestBlockX = pBox->srcx / pSurface->cxBlock;
        uint32_t const u32GuestBlockY = pBox->srcy / pSurface->cyBlock;
        Assert(u32GuestBlockX * pSurface->cxBlock == pBox->srcx);
        Assert(u32GuestBlockY * pSurface->cyBlock == pBox->srcy);
        uint32_t const cBlocksX = (pBox->w + pSurface->cxBlock - 1) / pSurface->cxBlock;
        uint32_t const cBlocksY = (pBox->h + pSurface->cyBlock - 1) / pSurface->cyBlock;
        AssertMsgReturn(cBlocksX && cBlocksY && pBox->d, ("Empty box %dx%dx%d\n", pBox->w, pBox->h, pBox->d), VERR_INTERNAL_ERROR);

        /* vmsvgaR3GmrTransfer verifies uGuestOffset.
         * srcx(u32GuestBlockX) and srcy(u32GuestBlockY) have been verified in vmsvga3dSurfaceDMA
         * to not cause 32 bit overflow when multiplied by cbBlock and cbGuestPitch.
         */
        uint64_t uGuestOffset = u32GuestBlockX * pSurface->cbBlock + u32GuestBlockY * cbGuestPitch;
        AssertReturn(uGuestOffset < UINT32_MAX, VERR_INVALID_PARAMETER);

        /* 3D texture needs additional processing. */
        ASSERT_GUEST_RETURN(   pBox->z < D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION
                            && pBox->d <= D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION
                            && pBox->d <= D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION - pBox->z,
                            VERR_INVALID_PARAMETER);
        ASSERT_GUEST_RETURN(   pBox->srcz < D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION
                            && pBox->d <= D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION
                            && pBox->d <= D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION - pBox->srcz,
                            VERR_INVALID_PARAMETER);

        uGuestOffset += pBox->srcz * pMipLevel->cbSurfacePlane;

        SVGA3dSurfaceImageId image;
        image.sid = pSurface->id;
        image.face = uHostFace;
        image.mipmap = uHostMipmap;

        SVGA3dBox box;
        box.x = pBox->x;
        box.y = pBox->y;
        box.z = pBox->z;
        box.w = pBox->w;
        box.h = pBox->h;
        box.d = pBox->d;

        VMSVGA3D_SURFACE_MAP const enmMap = transfer == SVGA3D_WRITE_HOST_VRAM
                                          ? VMSVGA3D_SURFACE_MAP_WRITE
                                          : VMSVGA3D_SURFACE_MAP_READ;

        VMSVGA3D_MAPPED_SURFACE map;
        rc = vmsvga3dBackSurfaceMap(pThisCC, &image, &box, enmMap, VMSVGA3D_MAP_F_NONE, &map);
        if (RT_SUCCESS(rc))
        {
#if 0
            if (box.w == 250 && box.h == 250 && box.d == 1 && enmMap == VMSVGA3D_SURFACE_MAP_READ)
            {
                DEBUG_BREAKPOINT_TEST();
                vmsvga3dMapWriteBmpFile(&map, "P");
            }
#endif
            /* Prepare parameters for vmsvgaR3GmrTransfer, which needs the host buffer address, size
             * and offset of the first scanline.
             */
            uint32_t cbLockedBuf = map.cbRowPitch * cBlocksY;
            if (pBackendSurface->enmResType == VMSVGA3D_RESTYPE_TEXTURE_3D)
                cbLockedBuf += map.cbDepthPitch * (pBox->d - 1); /// @todo why map does not compute this for 2D textures
            uint8_t *pu8LockedBuf = (uint8_t *)map.pvData;
            uint32_t offLockedBuf = 0;

            for (uint32_t iPlane = 0; iPlane < pBox->d; ++iPlane)
            {
                AssertBreak(uGuestOffset < UINT32_MAX);

                rc = vmsvgaR3GmrTransfer(pThis,
                                         pThisCC,
                                         transfer,
                                         pu8LockedBuf,
                                         cbLockedBuf,
                                         offLockedBuf,
                                         map.cbRowPitch,
                                         GuestPtr,
                                         (uint32_t)uGuestOffset,
                                         cbGuestPitch,
                                         cBlocksX * pSurface->cbBlock,
                                         cBlocksY);
                AssertRC(rc);

                uGuestOffset += pMipLevel->cbSurfacePlane;
                offLockedBuf += map.cbDepthPitch;
            }

            bool const fWritten = (transfer == SVGA3D_WRITE_HOST_VRAM);
            vmsvga3dBackSurfaceUnmap(pThisCC, &image, &map, fWritten);
        }
    }
    else
    {
        AssertMsgFailed(("Unsupported surface type %d\n", pBackendSurface->enmResType));
        rc = VERR_NOT_IMPLEMENTED;
    }

    return rc;
}


/**
 * Create D3D/OpenGL texture object for the specified surface.
 *
 * Surfaces are created when needed.
 *
 * @param   pThisCC             The device context.
 * @param   pContext            The context.
 * @param   idAssociatedContext Probably the same as pContext->id.
 * @param   pSurface            The surface to create the texture for.
 */
static DECLCALLBACK(int) vmsvga3dBackCreateTexture(PVGASTATECC pThisCC, PVMSVGA3DCONTEXT pContext, uint32_t idAssociatedContext,
                                     PVMSVGA3DSURFACE pSurface)

{
    RT_NOREF(pThisCC, pContext, idAssociatedContext, pSurface);

    AssertFailed();
    return VINF_SUCCESS;
}


/*
 * DX callbacks.
 */

static DECLCALLBACK(int) vmsvga3dBackDXDefineContext(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    RT_NOREF(pThisCC);

    /* Allocate a backend specific context structure. */
    PVMSVGA3DBACKENDDXCONTEXT pBackendDXContext = (PVMSVGA3DBACKENDDXCONTEXT)RTMemAllocZ(sizeof(VMSVGA3DBACKENDDXCONTEXT));
    AssertPtrReturn(pBackendDXContext, VERR_NO_MEMORY);
    pDXContext->pBackendDXContext = pBackendDXContext;

    LogFunc(("cid %d\n", pDXContext->cid));
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXDestroyContext(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    LogFunc(("cid %d\n", pDXContext->cid));

    if (pDXContext->pBackendDXContext)
    {
        /* Make sure that any pending draw calls are finished. */
        dxDeviceFlush(&pBackend->dxDevice);

        /* Clean up context resources. */
        VMSVGA3DBACKENDDXCONTEXT *pBackendDXContext = pDXContext->pBackendDXContext;

        for (uint32_t idxShaderState = 0; idxShaderState < RT_ELEMENTS(pBackendDXContext->resources.shaderState); ++idxShaderState)
        {
            ID3D11Buffer **papConstantBuffer = &pBackendDXContext->resources.shaderState[idxShaderState].constantBuffers[0];
            D3D_RELEASE_ARRAY(RT_ELEMENTS(pBackendDXContext->resources.shaderState[idxShaderState].constantBuffers), papConstantBuffer);
        }

        if (pBackendDXContext->paRenderTargetView)
        {
            for (uint32_t i = 0; i < pBackendDXContext->cRenderTargetView; ++i)
                D3D_RELEASE(pBackendDXContext->paRenderTargetView[i].u.pRenderTargetView);
        }
        if (pBackendDXContext->paDepthStencilView)
        {
            for (uint32_t i = 0; i < pBackendDXContext->cDepthStencilView; ++i)
                D3D_RELEASE(pBackendDXContext->paDepthStencilView[i].u.pDepthStencilView);
        }
        if (pBackendDXContext->paShaderResourceView)
        {
            for (uint32_t i = 0; i < pBackendDXContext->cShaderResourceView; ++i)
                D3D_RELEASE(pBackendDXContext->paShaderResourceView[i].u.pShaderResourceView);
        }
        if (pBackendDXContext->paElementLayout)
        {
            for (uint32_t i = 0; i < pBackendDXContext->cElementLayout; ++i)
                D3D_RELEASE(pBackendDXContext->paElementLayout[i].pElementLayout);
        }
        if (pBackendDXContext->papBlendState)
            D3D_RELEASE_ARRAY(pBackendDXContext->cBlendState, pBackendDXContext->papBlendState);
        if (pBackendDXContext->papDepthStencilState)
            D3D_RELEASE_ARRAY(pBackendDXContext->cDepthStencilState, pBackendDXContext->papDepthStencilState);
        if (pBackendDXContext->papRasterizerState)
            D3D_RELEASE_ARRAY(pBackendDXContext->cRasterizerState, pBackendDXContext->papRasterizerState);
        if (pBackendDXContext->papSamplerState)
            D3D_RELEASE_ARRAY(pBackendDXContext->cSamplerState, pBackendDXContext->papSamplerState);
        if (pBackendDXContext->paQuery)
        {
            for (uint32_t i = 0; i < pBackendDXContext->cQuery; ++i)
                dxDestroyQuery(&pBackendDXContext->paQuery[i]);
        }
        if (pBackendDXContext->paShader)
        {
            for (uint32_t i = 0; i < pBackendDXContext->cShader; ++i)
                dxDestroyShader(&pBackendDXContext->paShader[i]);
        }
        if (pBackendDXContext->paStreamOutput)
        {
            for (uint32_t i = 0; i < pBackendDXContext->cStreamOutput; ++i)
                dxDestroyStreamOutput(&pBackendDXContext->paStreamOutput[i]);
        }
        if (pBackendDXContext->paUnorderedAccessView)
        {
            for (uint32_t i = 0; i < pBackendDXContext->cUnorderedAccessView; ++i)
                D3D_RELEASE(pBackendDXContext->paUnorderedAccessView[i].u.pUnorderedAccessView);
        }
        if (pBackendDXContext->paVideoProcessor)
        {
            for (uint32_t i = 0; i < pBackendDXContext->cVideoProcessor; ++i)
                dxDestroyVideoProcessor(&pBackendDXContext->paVideoProcessor[i]);
        }
        if (pBackendDXContext->paVideoDecoderOutputView)
        {
            /** @todo dxViewDestroy? */
            for (uint32_t i = 0; i < pBackendDXContext->cVideoDecoderOutputView; ++i)
                D3D_RELEASE(pBackendDXContext->paVideoDecoderOutputView[i].u.pVideoDecoderOutputView);
        }
        if (pBackendDXContext->paVideoDecoder)
        {
            for (uint32_t i = 0; i < pBackendDXContext->cVideoDecoder; ++i)
                dxDestroyVideoDecoder(&pBackendDXContext->paVideoDecoder[i]);
        }
        if (pBackendDXContext->paVideoProcessorInputView)
        {
            for (uint32_t i = 0; i < pBackendDXContext->cVideoProcessorInputView; ++i)
                D3D_RELEASE(pBackendDXContext->paVideoProcessorInputView[i].u.pVideoProcessorInputView);
        }
        if (pBackendDXContext->paVideoProcessorOutputView)
        {
            for (uint32_t i = 0; i < pBackendDXContext->cVideoProcessorOutputView; ++i)
                D3D_RELEASE(pBackendDXContext->paVideoProcessorOutputView[i].u.pVideoProcessorOutputView);
        }

        RTMemFreeZ(pBackendDXContext->papBlendState, sizeof(pBackendDXContext->papBlendState[0]) * pBackendDXContext->cBlendState);
        RTMemFreeZ(pBackendDXContext->papDepthStencilState, sizeof(pBackendDXContext->papDepthStencilState[0]) * pBackendDXContext->cDepthStencilState);
        RTMemFreeZ(pBackendDXContext->papSamplerState, sizeof(pBackendDXContext->papSamplerState[0]) * pBackendDXContext->cSamplerState);
        RTMemFreeZ(pBackendDXContext->papRasterizerState, sizeof(pBackendDXContext->papRasterizerState[0]) * pBackendDXContext->cRasterizerState);
        RTMemFreeZ(pBackendDXContext->paElementLayout, sizeof(pBackendDXContext->paElementLayout[0]) * pBackendDXContext->cElementLayout);
        RTMemFreeZ(pBackendDXContext->paRenderTargetView, sizeof(pBackendDXContext->paRenderTargetView[0]) * pBackendDXContext->cRenderTargetView);
        RTMemFreeZ(pBackendDXContext->paDepthStencilView, sizeof(pBackendDXContext->paDepthStencilView[0]) * pBackendDXContext->cDepthStencilView);
        RTMemFreeZ(pBackendDXContext->paShaderResourceView, sizeof(pBackendDXContext->paShaderResourceView[0]) * pBackendDXContext->cShaderResourceView);
        RTMemFreeZ(pBackendDXContext->paQuery, sizeof(pBackendDXContext->paQuery[0]) * pBackendDXContext->cQuery);
        RTMemFreeZ(pBackendDXContext->paShader, sizeof(pBackendDXContext->paShader[0]) * pBackendDXContext->cShader);
        RTMemFreeZ(pBackendDXContext->paStreamOutput, sizeof(pBackendDXContext->paStreamOutput[0]) * pBackendDXContext->cStreamOutput);
        RTMemFreeZ(pBackendDXContext->paUnorderedAccessView, sizeof(pBackendDXContext->paUnorderedAccessView[0]) * pBackendDXContext->cUnorderedAccessView);
        RTMemFreeZ(pBackendDXContext->paVideoProcessor, sizeof(pBackendDXContext->paVideoProcessor[0]) * pBackendDXContext->cVideoProcessor);
        RTMemFreeZ(pBackendDXContext->paVideoDecoderOutputView, sizeof(pBackendDXContext->paVideoDecoderOutputView[0]) * pBackendDXContext->cVideoDecoderOutputView);
        RTMemFreeZ(pBackendDXContext->paVideoDecoder, sizeof(pBackendDXContext->paVideoDecoder[0]) * pBackendDXContext->cVideoDecoder);
        RTMemFreeZ(pBackendDXContext->paVideoProcessorInputView, sizeof(pBackendDXContext->paVideoProcessorInputView[0]) * pBackendDXContext->cVideoProcessorInputView);
        RTMemFreeZ(pBackendDXContext->paVideoProcessorOutputView, sizeof(pBackendDXContext->paVideoProcessorOutputView[0]) * pBackendDXContext->cVideoProcessorOutputView);

        RTMemFreeZ(pBackendDXContext, sizeof(*pBackendDXContext));
        pDXContext->pBackendDXContext = NULL;
    }
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXBindContext(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend, pDXContext);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXSwitchContext(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    /* The new context state will be applied by the generic DX code. */
    RT_NOREF(pThisCC, pDXContext);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXReadbackContext(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend, pDXContext);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXInvalidateContext(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackDXSetSingleConstantBuffer(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, uint32_t slot, SVGA3dShaderType type, SVGA3dSurfaceId sid, uint32_t offsetInBytes, uint32_t sizeInBytes)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    if (sid == SVGA_ID_INVALID)
    {
        uint32_t const idxShaderState = type - SVGA3D_SHADERTYPE_MIN;
        D3D_RELEASE(pDXContext->pBackendDXContext->resources.shaderState[idxShaderState].constantBuffers[slot]);
        return VINF_SUCCESS;
    }

    PVMSVGA3DSURFACE pSurface;
    int rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, sid, &pSurface);
    AssertRCReturn(rc, rc);

    PVMSVGA3DMIPMAPLEVEL pMipLevel;
    rc = vmsvga3dMipmapLevel(pSurface, 0, 0, &pMipLevel);
    AssertRCReturn(rc, rc);

    uint32_t const cbSurface = pMipLevel->cbSurface;
    ASSERT_GUEST_RETURN(   offsetInBytes < cbSurface
                        && sizeInBytes <= cbSurface - offsetInBytes, VERR_INVALID_PARAMETER);

    /* Constant buffers are created on demand. */
    Assert(pSurface->pBackendSurface == NULL);

    /* Upload the current data, if any. */
    D3D11_SUBRESOURCE_DATA *pInitialData = NULL;
    D3D11_SUBRESOURCE_DATA initialData;
    if (pMipLevel->pSurfaceData)
    {
        initialData.pSysMem          = (uint8_t *)pMipLevel->pSurfaceData + offsetInBytes;
        initialData.SysMemPitch      = sizeInBytes;
        initialData.SysMemSlicePitch = sizeInBytes;

        pInitialData = &initialData;

#ifdef LOG_ENABLED
        if (LogIs8Enabled())
        {
            float *pValuesF = (float *)initialData.pSysMem;
            for (unsigned i = 0; i < sizeInBytes / sizeof(float) / 4; ++i)
            {
                Log8(("ConstF /*%d*/ " FLOAT_FMT_STR ", " FLOAT_FMT_STR ", " FLOAT_FMT_STR ", " FLOAT_FMT_STR ",\n",
                      i, FLOAT_FMT_ARGS(pValuesF[i*4 + 0]), FLOAT_FMT_ARGS(pValuesF[i*4 + 1]), FLOAT_FMT_ARGS(pValuesF[i*4 + 2]), FLOAT_FMT_ARGS(pValuesF[i*4 + 3])));
            }
        }
#endif
    }

    D3D11_BUFFER_DESC bd;
    RT_ZERO(bd);
    bd.ByteWidth           = sizeInBytes;
    bd.Usage               = D3D11_USAGE_DEFAULT;
    bd.BindFlags           = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags      = 0;
    bd.MiscFlags           = 0;
    bd.StructureByteStride = 0;

    ID3D11Buffer *pBuffer = 0;
    HRESULT hr = pDevice->pDevice->CreateBuffer(&bd, pInitialData, &pBuffer);
    if (SUCCEEDED(hr))
    {
        uint32_t const idxShaderState = type - SVGA3D_SHADERTYPE_MIN;
        ID3D11Buffer **ppOldBuffer = &pDXContext->pBackendDXContext->resources.shaderState[idxShaderState].constantBuffers[slot];
        LogFunc(("constant buffer: [%u][%u]: sid = %u, %u, %u (%p -> %p)\n",
                 idxShaderState, slot, sid, offsetInBytes, sizeInBytes, *ppOldBuffer, pBuffer));
        D3D_RELEASE(*ppOldBuffer);
        *ppOldBuffer = pBuffer;
    }

    return VINF_SUCCESS;
}

static int dxSetShaderResources(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dShaderType type)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pDevice, VERR_INVALID_STATE);

    AssertReturn(type >= SVGA3D_SHADERTYPE_MIN && type < SVGA3D_SHADERTYPE_MAX, VERR_INVALID_PARAMETER);

    uint32_t const idxShaderState = type - SVGA3D_SHADERTYPE_MIN;
    uint32_t const *pSRIds = &pDXContext->svgaDXContext.shaderState[idxShaderState].shaderResources[0];
    ID3D11ShaderResourceView *papShaderResourceView[SVGA3D_DX_MAX_SRVIEWS];
    for (uint32_t i = 0; i < SVGA3D_DX_MAX_SRVIEWS; ++i)
    {
        SVGA3dShaderResourceViewId const shaderResourceViewId = pSRIds[i];
        if (shaderResourceViewId != SVGA3D_INVALID_ID)
        {
            ASSERT_GUEST_RETURN(shaderResourceViewId < pDXContext->pBackendDXContext->cShaderResourceView, VERR_INVALID_PARAMETER);

            DXVIEW *pDXView = &pDXContext->pBackendDXContext->paShaderResourceView[shaderResourceViewId];
            Assert(pDXView->u.pShaderResourceView);
            papShaderResourceView[i] = pDXView->u.pShaderResourceView;
        }
        else
            papShaderResourceView[i] = NULL;
    }

    dxShaderResourceViewSet(pDXDevice, type, 0, SVGA3D_DX_MAX_SRVIEWS, papShaderResourceView);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXSetShaderResources(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, uint32_t startView, SVGA3dShaderType type, uint32_t cShaderResourceViewId, SVGA3dShaderResourceViewId const *paShaderResourceViewId)
{
    /* Shader resources will be set in setupPipeline. */
    RT_NOREF(pThisCC, pDXContext, startView, type, cShaderResourceViewId, paShaderResourceViewId);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXSetShader(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dShaderId shaderId, SVGA3dShaderType type)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend, pDXContext);

    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    RT_NOREF(shaderId, type);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXSetSamplers(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, uint32_t startSampler, SVGA3dShaderType type, uint32_t cSamplerId, SVGA3dSamplerId const *paSamplerId)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    ID3D11SamplerState *papSamplerState[SVGA3D_DX_MAX_SAMPLERS];
    for (uint32_t i = 0; i < cSamplerId; ++i)
    {
        SVGA3dSamplerId samplerId = paSamplerId[i];
        if (samplerId != SVGA3D_INVALID_ID)
        {
            ASSERT_GUEST_RETURN(samplerId < pDXContext->pBackendDXContext->cSamplerState, VERR_INVALID_PARAMETER);
            papSamplerState[i] = pDXContext->pBackendDXContext->papSamplerState[samplerId];
        }
        else
            papSamplerState[i] = NULL;
    }

    dxSamplerSet(pDevice, type, startSampler, cSamplerId, papSamplerState);
    return VINF_SUCCESS;
}


static void vboxDXMatchShaderInput(DXSHADER *pDXShader, DXSHADER *pDXShaderPrior)
{
    /* For each input generic attribute of the shader find corresponding entry in the prior shader. */
    for (uint32_t i = 0; i < pDXShader->shaderInfo.cInputSignature; ++i)
    {
        SVGA3dDXSignatureEntry const *pSignatureEntry = &pDXShader->shaderInfo.aInputSignature[i];
        DXShaderAttributeSemantic *pSemantic = &pDXShader->shaderInfo.aInputSemantic[i];

        if (pSignatureEntry->semanticName != SVGADX_SIGNATURE_SEMANTIC_NAME_UNDEFINED)
            continue;

        int iMatch = -1;
        for (uint32_t iPrior = 0; iPrior < pDXShaderPrior->shaderInfo.cOutputSignature; ++iPrior)
        {
            SVGA3dDXSignatureEntry const *pPriorSignatureEntry = &pDXShaderPrior->shaderInfo.aOutputSignature[iPrior];

            if (pPriorSignatureEntry->semanticName != SVGADX_SIGNATURE_SEMANTIC_NAME_UNDEFINED)
                continue;

            if (pPriorSignatureEntry->registerIndex == pSignatureEntry->registerIndex)
            {
                iMatch = iPrior;
                if (pPriorSignatureEntry->mask == pSignatureEntry->mask)
                    break; /* Exact match, no need to continue search. */
            }
        }

        if (iMatch >= 0)
        {
            SVGA3dDXSignatureEntry const *pPriorSignatureEntry = &pDXShaderPrior->shaderInfo.aOutputSignature[iMatch];
            DXShaderAttributeSemantic const *pPriorSemantic = &pDXShaderPrior->shaderInfo.aOutputSemantic[iMatch];

            Assert(pPriorSignatureEntry->registerIndex == pSignatureEntry->registerIndex);
            Assert((pPriorSignatureEntry->mask & pSignatureEntry->mask) == pSignatureEntry->mask);
            RT_NOREF(pPriorSignatureEntry);

            pSemantic->SemanticIndex = pPriorSemantic->SemanticIndex;
        }
    }
}


static void vboxDXMatchShaderSignatures(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, DXSHADER *pDXShader)
{
    SVGA3dShaderId const shaderIdVS = pDXContext->svgaDXContext.shaderState[SVGA3D_SHADERTYPE_VS - SVGA3D_SHADERTYPE_MIN].shaderId;
    SVGA3dShaderId const shaderIdHS = pDXContext->svgaDXContext.shaderState[SVGA3D_SHADERTYPE_HS - SVGA3D_SHADERTYPE_MIN].shaderId;
    SVGA3dShaderId const shaderIdDS = pDXContext->svgaDXContext.shaderState[SVGA3D_SHADERTYPE_DS - SVGA3D_SHADERTYPE_MIN].shaderId;
    SVGA3dShaderId const shaderIdGS = pDXContext->svgaDXContext.shaderState[SVGA3D_SHADERTYPE_GS - SVGA3D_SHADERTYPE_MIN].shaderId;
    SVGA3dShaderId const shaderIdPS = pDXContext->svgaDXContext.shaderState[SVGA3D_SHADERTYPE_PS - SVGA3D_SHADERTYPE_MIN].shaderId;

    /* Try to fix the input semantic indices. Output is usually not changed. */
    switch (pDXShader->enmShaderType)
    {
        case SVGA3D_SHADERTYPE_VS:
        {
            /* Match input to input layout, which sets generic semantic indices to the source registerIndex (dxCreateInputLayout). */
            for (uint32_t i = 0; i < pDXShader->shaderInfo.cInputSignature; ++i)
            {
                SVGA3dDXSignatureEntry const *pSignatureEntry = &pDXShader->shaderInfo.aInputSignature[i];
                DXShaderAttributeSemantic *pSemantic = &pDXShader->shaderInfo.aInputSemantic[i];

                if (pSignatureEntry->semanticName != SVGADX_SIGNATURE_SEMANTIC_NAME_UNDEFINED)
                    continue;

                pSemantic->SemanticIndex = pSignatureEntry->registerIndex;
            }
            break;
        }
        case SVGA3D_SHADERTYPE_HS:
        {
            /* Input of a HS shader is the output of VS. */
            DXSHADER *pDXShaderPrior;
            if (shaderIdVS != SVGA3D_INVALID_ID)
                pDXShaderPrior = &pDXContext->pBackendDXContext->paShader[shaderIdVS];
            else
                pDXShaderPrior = NULL;

            if (pDXShaderPrior)
                vboxDXMatchShaderInput(pDXShader, pDXShaderPrior);

            break;
        }
        case SVGA3D_SHADERTYPE_DS:
        {
            /* Input of a DS shader is the output of HS. */
            DXSHADER *pDXShaderPrior;
            if (shaderIdHS != SVGA3D_INVALID_ID)
                pDXShaderPrior = &pDXContext->pBackendDXContext->paShader[shaderIdHS];
            else
                pDXShaderPrior = NULL;

            if (pDXShaderPrior)
                vboxDXMatchShaderInput(pDXShader, pDXShaderPrior);

            break;
        }
        case SVGA3D_SHADERTYPE_GS:
        {
            /* Input signature of a GS shader is the output of DS or VS. */
            DXSHADER *pDXShaderPrior;
            if (shaderIdDS != SVGA3D_INVALID_ID)
                 pDXShaderPrior = &pDXContext->pBackendDXContext->paShader[shaderIdDS];
            else if (shaderIdVS != SVGA3D_INVALID_ID)
                pDXShaderPrior = &pDXContext->pBackendDXContext->paShader[shaderIdVS];
            else
                pDXShaderPrior = NULL;

            if (pDXShaderPrior)
            {
                /* If GS shader does not have input signature (Windows guest can do that),
                 * then assign the prior shader signature as GS input.
                 */
                if (pDXShader->shaderInfo.cInputSignature == 0)
                {
                    pDXShader->shaderInfo.cInputSignature = pDXShaderPrior->shaderInfo.cOutputSignature;
                    memcpy(pDXShader->shaderInfo.aInputSignature,
                           pDXShaderPrior->shaderInfo.aOutputSignature,
                           pDXShaderPrior->shaderInfo.cOutputSignature * sizeof(SVGA3dDXSignatureEntry));
                    memcpy(pDXShader->shaderInfo.aInputSemantic,
                           pDXShaderPrior->shaderInfo.aOutputSemantic,
                           pDXShaderPrior->shaderInfo.cOutputSignature * sizeof(DXShaderAttributeSemantic));
                }
                else
                    vboxDXMatchShaderInput(pDXShader, pDXShaderPrior);
            }

            /* Output signature of a GS shader is the input of the pixel shader. */
            if (shaderIdPS != SVGA3D_INVALID_ID)
            {
                /* If GS shader does not have output signature (Windows guest can do that),
                 * then assign the PS shader signature as GS output.
                 */
                if (pDXShader->shaderInfo.cOutputSignature == 0)
                {
                    DXSHADER const *pDXShaderPosterior = &pDXContext->pBackendDXContext->paShader[shaderIdPS];
                    pDXShader->shaderInfo.cOutputSignature = pDXShaderPosterior->shaderInfo.cInputSignature;
                    memcpy(pDXShader->shaderInfo.aOutputSignature,
                           pDXShaderPosterior->shaderInfo.aInputSignature,
                           pDXShaderPosterior->shaderInfo.cInputSignature * sizeof(SVGA3dDXSignatureEntry));
                    memcpy(pDXShader->shaderInfo.aOutputSemantic,
                           pDXShaderPosterior->shaderInfo.aInputSemantic,
                           pDXShaderPosterior->shaderInfo.cInputSignature * sizeof(DXShaderAttributeSemantic));
                }
            }

            SVGA3dStreamOutputId const soid = pDXContext->svgaDXContext.streamOut.soid;
            if (soid != SVGA3D_INVALID_ID)
            {
                ASSERT_GUEST_RETURN_VOID(soid < pDXContext->pBackendDXContext->cStreamOutput);

                /* Set semantic names and indices for SO declaration entries according to the shader output. */
                SVGACOTableDXStreamOutputEntry const *pStreamOutputEntry = &pDXContext->cot.paStreamOutput[soid];
                DXSTREAMOUTPUT *pDXStreamOutput = &pDXContext->pBackendDXContext->paStreamOutput[soid];

                if (pDXStreamOutput->cDeclarationEntry == 0)
                {
                    int rc = dxDefineStreamOutput(pThisCC, pDXContext, soid, pStreamOutputEntry, pDXShader);
                    AssertRCReturnVoid(rc);
#ifdef LOG_ENABLED
                    Log6(("Stream output declaration:\n\n"));
                    Log6(("Stream SemanticName   SemanticIndex StartComponent ComponentCount OutputSlot\n"));
                    Log6(("------ -------------- ------------- -------------- -------------- ----------\n"));
                    for (unsigned i = 0; i < pDXStreamOutput->cDeclarationEntry; ++i)
                    {
                        D3D11_SO_DECLARATION_ENTRY *p = &pDXStreamOutput->aDeclarationEntry[i];
                        Log6(("%d      %-14s %d             %d              %d              %d\n",
                              p->Stream, p->SemanticName, p->SemanticIndex, p->StartComponent, p->ComponentCount, p->OutputSlot));
                    }
                    Log6(("\n"));
#endif

                }
            }
            break;
        }
        case SVGA3D_SHADERTYPE_PS:
        {
            /* Input of a PS shader is the output of GS, DS or VS. */
            DXSHADER *pDXShaderPrior;
            if (shaderIdGS != SVGA3D_INVALID_ID)
                pDXShaderPrior = &pDXContext->pBackendDXContext->paShader[shaderIdGS];
            else if (shaderIdDS != SVGA3D_INVALID_ID)
                 pDXShaderPrior = &pDXContext->pBackendDXContext->paShader[shaderIdDS];
            else if (shaderIdVS != SVGA3D_INVALID_ID)
                pDXShaderPrior = &pDXContext->pBackendDXContext->paShader[shaderIdVS];
            else
                pDXShaderPrior = NULL;

            if (pDXShaderPrior)
                vboxDXMatchShaderInput(pDXShader, pDXShaderPrior);
            break;
        }
        default:
           break;
    }

    /* Intermediate shaders normally have both input and output signatures. However it is ok if they do not.
     * Just catch this unusual case in order to see if everything is fine.
     */
    Assert(   (   pDXShader->enmShaderType == SVGA3D_SHADERTYPE_VS
               || pDXShader->enmShaderType == SVGA3D_SHADERTYPE_PS
               || pDXShader->enmShaderType == SVGA3D_SHADERTYPE_CS)
           || (pDXShader->shaderInfo.cInputSignature && pDXShader->shaderInfo.cOutputSignature));
}


static void vboxDXUpdateVSInputSignature(PVMSVGA3DDXCONTEXT pDXContext, DXSHADER *pDXShader)
{
    SVGA3dElementLayoutId const elementLayoutId = pDXContext->svgaDXContext.inputAssembly.layoutId;
    if (elementLayoutId != SVGA3D_INVALID_ID)
    {
        SVGACOTableDXElementLayoutEntry const *pElementLayout = &pDXContext->cot.paElementLayout[elementLayoutId];
        for (uint32_t i = 0; i < RT_MIN(pElementLayout->numDescs, pDXShader->shaderInfo.cInputSignature); ++i)
        {
            SVGA3dInputElementDesc const *pElementDesc = &pElementLayout->descs[i];
            SVGA3dDXSignatureEntry *pSignatureEntry = &pDXShader->shaderInfo.aInputSignature[i];
            pSignatureEntry->componentType = DXShaderComponentTypeFromFormat(pElementDesc->format);
        }
    }
}


static void dxCreateInputLayout(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dElementLayoutId elementLayoutId, DXSHADER *pDXShader)
{
    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturnVoid(pDevice->pDevice);

    SVGACOTableDXElementLayoutEntry const *pEntry = &pDXContext->cot.paElementLayout[elementLayoutId];
    DXELEMENTLAYOUT *pDXElementLayout = &pDXContext->pBackendDXContext->paElementLayout[elementLayoutId];

    if (pDXElementLayout->cElementDesc == 0)
    {
        /* Semantic name is not interpreted by D3D, therefore arbitrary names can be used
         * if they are consistent between the element layout and shader input signature.
         * "In general, data passed between pipeline stages is completely generic and is not uniquely
         * interpreted by the system; arbitrary semantics are allowed ..."
         *
         * However D3D runtime insists that "SemanticName string ("POSITIO1") cannot end with a number."
         *
         * System-Value semantics ("SV_*") between shaders require proper names of course.
         * But they are irrelevant for input attributes.
         */
        pDXElementLayout->cElementDesc = pEntry->numDescs;
        for (uint32_t i = 0; i < pEntry->numDescs; ++i)
        {
            D3D11_INPUT_ELEMENT_DESC *pDst = &pDXElementLayout->aElementDesc[i];
            SVGA3dInputElementDesc const *pSrc = &pEntry->descs[i];
            pDst->SemanticName         = "ATTRIB";
            pDst->SemanticIndex        = pSrc->inputRegister;
            pDst->Format               = vmsvgaDXSurfaceFormat2Dxgi(pSrc->format);
            Assert(pDst->Format != DXGI_FORMAT_UNKNOWN);
            pDst->InputSlot            = pSrc->inputSlot;
            pDst->AlignedByteOffset    = pSrc->alignedByteOffset;
            pDst->InputSlotClass       = (D3D11_INPUT_CLASSIFICATION)pSrc->inputSlotClass;
            pDst->InstanceDataStepRate = pSrc->instanceDataStepRate;
        }
    }

    HRESULT hr = pDevice->pDevice->CreateInputLayout(pDXElementLayout->aElementDesc,
                                                     pDXElementLayout->cElementDesc,
                                                     pDXShader->pvDXBC,
                                                     pDXShader->cbDXBC,
                                                     &pDXElementLayout->pElementLayout);
    Assert(SUCCEEDED(hr)); RT_NOREF(hr);
}


static void dxSetConstantBuffers(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
//DEBUG_BREAKPOINT_TEST();
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    VMSVGA3DBACKENDDXCONTEXT *pBackendDXContext = pDXContext->pBackendDXContext;

    AssertCompile(RT_ELEMENTS(pBackendDXContext->resources.shaderState[0].constantBuffers) == SVGA3D_DX_MAX_CONSTBUFFERS);

    for (uint32_t idxShaderState = 0; idxShaderState < SVGA3D_NUM_SHADERTYPE; ++idxShaderState)
    {
        SVGA3dShaderType const shaderType = (SVGA3dShaderType)(idxShaderState + SVGA3D_SHADERTYPE_MIN);
        for (uint32_t idxSlot = 0; idxSlot < SVGA3D_DX_MAX_CONSTBUFFERS; ++idxSlot)
        {
            ID3D11Buffer **pBufferContext = &pBackendDXContext->resources.shaderState[idxShaderState].constantBuffers[idxSlot];
            ID3D11Buffer **pBufferPipeline = &pBackend->resources.shaderState[idxShaderState].constantBuffers[idxSlot];
            if (*pBufferContext != *pBufferPipeline)
            {
                LogFunc(("constant buffer: [%u][%u]: %p -> %p\n",
                         idxShaderState, idxSlot, *pBufferPipeline, *pBufferContext));
                dxConstantBufferSet(pDXDevice, idxSlot, shaderType, *pBufferContext);

                if (*pBufferContext)
                   (*pBufferContext)->AddRef();
                D3D_RELEASE(*pBufferPipeline);
                *pBufferPipeline = *pBufferContext;
            }
        }
    }
}

static void dxSetVertexBuffers(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    ID3D11Buffer *paResources[SVGA3D_DX_MAX_VERTEXBUFFERS];
    UINT paStride[SVGA3D_DX_MAX_VERTEXBUFFERS];
    UINT paOffset[SVGA3D_DX_MAX_VERTEXBUFFERS];

    int32_t idxMinSlot = SVGA3D_DX_MAX_VERTEXBUFFERS;
    int32_t idxMaxSlot = -1;
    for (int32_t i = 0; i < SVGA3D_DX_MAX_VERTEXBUFFERS; ++i)
    {
        SVGA3dBufferBinding const *pBufferContext = &pDXContext->svgaDXContext.inputAssembly.vertexBuffers[i];
        SVGA3dBufferBinding *pBufferPipeline = &pBackend->svgaDXContext.inputAssembly.vertexBuffers[i];

        if (   pBufferContext->bufferId != pBufferPipeline->bufferId
            || pBufferContext->stride != pBufferPipeline->stride
            || pBufferContext->offset != pBufferPipeline->offset)
        {
            /* The slot has a new buffer. */
            LogFunc(("vb[%u]: sid = %u, stride %d, off %d -> sid = %u, stride %d, off %d\n",
                     i, pBufferPipeline->bufferId, pBufferPipeline->stride, pBufferPipeline->offset,
                     pBufferContext->bufferId, pBufferContext->stride, pBufferContext->offset));

            *pBufferPipeline = *pBufferContext;

            idxMaxSlot = RT_MAX(idxMaxSlot, i);
            idxMinSlot = RT_MIN(idxMinSlot, i);
        }
#ifdef LOG_ENABLED
        else if (pBufferPipeline->bufferId != SVGA3D_INVALID_ID)
        {
            LogFunc(("vb[%u]: sid = %u, stride %d, off %d\n",
                     i, pBufferPipeline->bufferId, pBufferPipeline->stride, pBufferPipeline->offset));
        }
#endif
        PVMSVGA3DSURFACE pSurface = NULL;
        ID3D11Buffer *pBuffer = NULL;
        if (pBufferPipeline->bufferId != SVGA3D_INVALID_ID)
        {
            ID3D11Resource *pResource;
            int rc = dxEnsureResource(pThisCC, pBufferPipeline->bufferId, &pSurface, &pResource);
            if (   RT_SUCCESS(rc)
                && pSurface->pBackendSurface->enmResType == VMSVGA3D_RESTYPE_BUFFER)
                pBuffer = (ID3D11Buffer *)pResource;
            else
                AssertMsgFailed(("%Rrc %p %d\n", rc, pSurface->pBackendSurface, pSurface->pBackendSurface ? pSurface->pBackendSurface->enmResType : VMSVGA3D_RESTYPE_NONE));
        }

        paResources[i] = pBuffer;
        if (pBuffer)
        {
            LogFunc(("vb[%u]: %p\n", i, pBuffer));

            /* DX11 supports stride up tp 2048. Ignore large values (> 40000) that Ubuntu guest might send. */
            paStride[i] = pBufferPipeline->stride <= 2048 ? pBufferPipeline->stride : 0;

            if (   paStride[i] <= pSurface->paMipmapLevels[0].cbSurface
                && pBufferPipeline->offset <= pSurface->paMipmapLevels[0].cbSurface - paStride[i])
                paOffset[i] = pBufferPipeline->offset;
            else
            {
                paStride[i] = 0;
                paOffset[i] = 0;
            }
        }
        else
        {
            paStride[i] = 0;
            paOffset[i] = 0;
        }
    }

    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    LogFunc(("idxMinSlot = %d, idxMaxSlot = %d\n", idxMinSlot, idxMaxSlot));
    if (idxMaxSlot >= 0)
        pDXDevice->pImmediateContext->IASetVertexBuffers(idxMinSlot, (idxMaxSlot - idxMinSlot) + 1,
                                                         &paResources[idxMinSlot], &paStride[idxMinSlot], &paOffset[idxMinSlot]);
}

static void dxSetIndexBuffer(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    SVGADXContextMobFormat *pPipelineState = &pBackend->svgaDXContext;
    SVGADXContextMobFormat const *pContextState = &pDXContext->svgaDXContext;

    if (   pPipelineState->inputAssembly.indexBufferSid != pContextState->inputAssembly.indexBufferSid
        || pPipelineState->inputAssembly.indexBufferOffset != pContextState->inputAssembly.indexBufferOffset
        || pPipelineState->inputAssembly.indexBufferFormat != pContextState->inputAssembly.indexBufferFormat)
    {
        LogFunc(("ib: sid = %u, offset %u, fmt %u -> sid = %u, offset %u, fmt %u\n",
                 pPipelineState->inputAssembly.indexBufferSid, pPipelineState->inputAssembly.indexBufferOffset, pPipelineState->inputAssembly.indexBufferFormat,
                 pContextState->inputAssembly.indexBufferSid, pContextState->inputAssembly.indexBufferOffset, pContextState->inputAssembly.indexBufferFormat));

        pPipelineState->inputAssembly.indexBufferSid = pContextState->inputAssembly.indexBufferSid;
        pPipelineState->inputAssembly.indexBufferOffset = pContextState->inputAssembly.indexBufferOffset;
        pPipelineState->inputAssembly.indexBufferFormat = pContextState->inputAssembly.indexBufferFormat;
    }
#ifdef LOG_ENABLED
    else if (pPipelineState->inputAssembly.indexBufferSid != SVGA3D_INVALID_ID)
    {
        LogFunc(("ib: sid = %u, offset %u, fmt %u\n",
                 pPipelineState->inputAssembly.indexBufferSid, pPipelineState->inputAssembly.indexBufferOffset, pPipelineState->inputAssembly.indexBufferFormat));
    }
#endif

    ID3D11Buffer *pBuffer = NULL;
    if (pPipelineState->inputAssembly.indexBufferSid != SVGA3D_INVALID_ID)
    {
        PVMSVGA3DSURFACE pSurface;
        ID3D11Resource *pResource;
        int rc = dxEnsureResource(pThisCC, pPipelineState->inputAssembly.indexBufferSid, &pSurface, &pResource);
        if (   RT_SUCCESS(rc)
            && pSurface->pBackendSurface->enmResType == VMSVGA3D_RESTYPE_BUFFER)
            pBuffer = (ID3D11Buffer *)pResource;
        else
            AssertMsgFailed(("%Rrc %p %d\n", rc, pSurface->pBackendSurface, pSurface->pBackendSurface ? pSurface->pBackendSurface->enmResType : VMSVGA3D_RESTYPE_NONE));
    }

    DXGI_FORMAT enmDxgiFormat;
    UINT Offset;
    if (pBuffer)
    {
        enmDxgiFormat = vmsvgaDXSurfaceFormat2Dxgi((SVGA3dSurfaceFormat)pPipelineState->inputAssembly.indexBufferFormat);
        AssertReturnVoid(enmDxgiFormat == DXGI_FORMAT_R16_UINT || enmDxgiFormat == DXGI_FORMAT_R32_UINT);
        Offset = pPipelineState->inputAssembly.indexBufferOffset;
    }
    else
    {
        enmDxgiFormat = DXGI_FORMAT_UNKNOWN;
        Offset = 0;
    }

    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    pDXDevice->pImmediateContext->IASetIndexBuffer(pBuffer, enmDxgiFormat, Offset);
}

#ifdef LOG_ENABLED
static void dxDbgLogVertexElement(DXGI_FORMAT Format, void const *pvElementData)
{
    switch (Format)
    {
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
        {
            float const *pValues = (float const *)pvElementData;
            Log8(("{ " FLOAT_FMT_STR ", " FLOAT_FMT_STR ", " FLOAT_FMT_STR ", " FLOAT_FMT_STR " },",
                 FLOAT_FMT_ARGS(pValues[0]), FLOAT_FMT_ARGS(pValues[1]), FLOAT_FMT_ARGS(pValues[2]), FLOAT_FMT_ARGS(pValues[3])));
            break;
        }
        case DXGI_FORMAT_R32G32B32_FLOAT:
        {
            float const *pValues = (float const *)pvElementData;
            Log8(("{ " FLOAT_FMT_STR ", " FLOAT_FMT_STR ", " FLOAT_FMT_STR " },",
                 FLOAT_FMT_ARGS(pValues[0]), FLOAT_FMT_ARGS(pValues[1]), FLOAT_FMT_ARGS(pValues[2])));
            break;
        }
        case DXGI_FORMAT_R32G32_FLOAT:
        {
            float const *pValues = (float const *)pvElementData;
            Log8(("{ " FLOAT_FMT_STR ", " FLOAT_FMT_STR " },",
                 FLOAT_FMT_ARGS(pValues[0]), FLOAT_FMT_ARGS(pValues[1])));
            break;
        }
        case DXGI_FORMAT_R32_FLOAT:
        {
            float const *pValues = (float const *)pvElementData;
            Log8(("{ " FLOAT_FMT_STR " },",
                 FLOAT_FMT_ARGS(pValues[0])));
            break;
        }
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        {
            uint16_t const *pValues = (uint16_t const *)pvElementData;
            Log8(("{ /*f16*/ " FLOAT_FMT_STR ", " FLOAT_FMT_STR ", " FLOAT_FMT_STR ", " FLOAT_FMT_STR " },",
                 FLOAT_FMT_ARGS(float16ToFloat(pValues[0])), FLOAT_FMT_ARGS(float16ToFloat(pValues[1])),
                 FLOAT_FMT_ARGS(float16ToFloat(pValues[2])), FLOAT_FMT_ARGS(float16ToFloat(pValues[3]))));
            break;
        }
        case DXGI_FORMAT_R16G16_FLOAT:
        {
            uint16_t const *pValues = (uint16_t const *)pvElementData;
            Log8(("{ /*f16*/ " FLOAT_FMT_STR ", " FLOAT_FMT_STR " },",
                 FLOAT_FMT_ARGS(float16ToFloat(pValues[0])), FLOAT_FMT_ARGS(float16ToFloat(pValues[1]))));
            break;
        }
        case DXGI_FORMAT_R32G32_SINT:
        {
            int32_t const *pValues = (int32_t const *)pvElementData;
            Log8(("{ %d, %d },",
                 pValues[0], pValues[1]));
            break;
        }
        case DXGI_FORMAT_R32G32_UINT:
        {
            uint32_t const *pValues = (uint32_t const *)pvElementData;
            Log8(("{ %u, %u },",
                 pValues[0], pValues[1]));
            break;
        }
        case DXGI_FORMAT_R32_SINT:
        {
            int32_t const *pValues = (int32_t const *)pvElementData;
            Log8(("{ %d },",
                 pValues[0]));
            break;
        }
        case DXGI_FORMAT_R32_UINT:
        {
            uint32_t const *pValues = (uint32_t const *)pvElementData;
            Log8(("{ %u },",
                 pValues[0]));
            break;
        }
        case DXGI_FORMAT_R16G16B16A16_SINT:
        {
            int16_t const *pValues = (int16_t const *)pvElementData;
            Log8(("{ /*s16*/ %d, %d, %d, %d },",
                 pValues[0], pValues[1], pValues[2], pValues[3]));
            break;
        }
        case DXGI_FORMAT_R16G16_SINT:
        {
            int16_t const *pValues = (int16_t const *)pvElementData;
            Log8(("{ /*s16*/ %d, %d },",
                 pValues[0], pValues[1]));
            break;
        }
        case DXGI_FORMAT_R16G16_UINT:
        {
            uint16_t const *pValues = (uint16_t const *)pvElementData;
            Log8(("{ /*u16*/ %u, %u },",
                 pValues[0], pValues[1]));
            break;
        }
        case DXGI_FORMAT_R16G16_SNORM:
        {
            int16_t const *pValues = (int16_t const *)pvElementData;
            Log8(("{ /*sn16*/ 0x%x, 0x%x },",
                 pValues[0], pValues[1]));
            break;
        }
        case DXGI_FORMAT_R16G16_UNORM:
        {
            uint16_t const *pValues = (uint16_t const *)pvElementData;
            Log8(("{ /*un16*/ 0x%x, 0x%x },",
                 pValues[0], pValues[1]));
            break;
        }
        case DXGI_FORMAT_R16_UINT:
        {
            uint16_t const *pValues = (uint16_t const *)pvElementData;
            Log8(("{ /*u16*/ %u },",
                 pValues[0]));
            break;
        }
        case DXGI_FORMAT_R8G8B8A8_SINT:
        {
            uint8_t const *pValues = (uint8_t const *)pvElementData;
            Log8(("{ /*8sint*/  %d, %d, %d, %d },",
                 pValues[0], pValues[1], pValues[2], pValues[3]));
            break;
        }
        case DXGI_FORMAT_R8G8B8A8_UINT:
        {
            uint8_t const *pValues = (uint8_t const *)pvElementData;
            Log8(("{ /*8uint*/  %u, %u, %u, %u },",
                 pValues[0], pValues[1], pValues[2], pValues[3]));
            break;
        }
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        {
            uint8_t const *pValues = (uint8_t const *)pvElementData;
            Log8(("{ /*8unorm*/  %u, %u, %u, %u },",
                 pValues[0], pValues[1], pValues[2], pValues[3]));
            break;
        }
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        {
            uint8_t const *pValues = (uint8_t const *)pvElementData;
            Log8(("{ /*8unorm*/  %u, %u, %u, %u },",
                 pValues[0], pValues[1], pValues[2], pValues[3]));
            break;
        }
        case DXGI_FORMAT_R8G8_UINT:
        {
            uint8_t const *pValues = (uint8_t const *)pvElementData;
            Log8(("{ /*8uint*/  %u, %u },",
                 pValues[0], pValues[1]));
            break;
        }
        case DXGI_FORMAT_R8G8_UNORM:
        {
            uint8_t const *pValues = (uint8_t const *)pvElementData;
            Log8(("{ /*8unorm*/  %u, %u },",
                 pValues[0], pValues[1]));
            break;
        }
        case DXGI_FORMAT_R8_UINT:
        {
            uint8_t const *pValues = (uint8_t const *)pvElementData;
            Log8(("{ /*8unorm*/  %u },",
                 pValues[0]));
            break;
        }
        default:
            Log8(("{ ??? DXGI_FORMAT %d },",
                 Format));
            AssertFailed();
    }
}


static void dxDbgDumpVertexData(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, uint32_t vertexCount, uint32_t startVertexLocation)
{
    for (uint32_t iSlot = 0; iSlot < SVGA3D_DX_MAX_VERTEXBUFFERS; ++iSlot)
    {
        SVGA3dBufferBinding const *pVBInfo = &pDXContext->svgaDXContext.inputAssembly.vertexBuffers[iSlot];
        if (pVBInfo->bufferId == SVGA3D_INVALID_ID)
            continue;

        PVMSVGA3DSURFACE pSurface = NULL;
        int rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, pVBInfo->bufferId, &pSurface);
        AssertContinue(RT_SUCCESS(rc));
        AssertContinue(pSurface->pBackendSurface->u.pBuffer && pSurface->pBackendSurface->enmResType == VMSVGA3D_RESTYPE_BUFFER);

        SVGA3dSurfaceImageId image;
        image.sid = pVBInfo->bufferId;
        image.face = 0;
        image.mipmap = 0;

        VMSVGA3D_MAPPED_SURFACE map;
        rc = vmsvga3dBackSurfaceMap(pThisCC, &image, NULL, VMSVGA3D_SURFACE_MAP_READ, VMSVGA3D_MAP_F_NONE, &map);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            uint8_t const *pu8VertexData = (uint8_t *)map.pvData;
            pu8VertexData += pVBInfo->offset;
            pu8VertexData += startVertexLocation * pVBInfo->stride;

            SVGA3dElementLayoutId const elementLayoutId = pDXContext->svgaDXContext.inputAssembly.layoutId;
            DXELEMENTLAYOUT *pDXElementLayout = &pDXContext->pBackendDXContext->paElementLayout[elementLayoutId];
            Assert(pDXElementLayout->cElementDesc > 0);

            Log8(("Vertex buffer dump: sid = %u, vertexCount %u, startVertexLocation %d, offset = %d, stride = %d:\n",
                  pVBInfo->bufferId, vertexCount, startVertexLocation, pVBInfo->offset, pVBInfo->stride));

            for (uint32_t v = 0; v < vertexCount; ++v)
            {
                Log8(("slot[%u] /* v%u */ { ", iSlot, startVertexLocation + v));

                for (uint32_t iElement = 0; iElement < pDXElementLayout->cElementDesc; ++iElement)
                {
                    D3D11_INPUT_ELEMENT_DESC *pElement = &pDXElementLayout->aElementDesc[iElement];
                    if (pElement->InputSlot == iSlot)
                        dxDbgLogVertexElement(pElement->Format, pu8VertexData + pElement->AlignedByteOffset);
                }

                Log8((" },\n"));

                if (pVBInfo->stride == 0)
                    break;

                pu8VertexData += pVBInfo->stride;
            }

            vmsvga3dBackSurfaceUnmap(pThisCC, &image, &map, /* fWritten =  */ false);
        }
    }
}


static void dxDbgDumpIndexedVertexData(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, uint32_t indexCount, uint32_t startIndexLocation, int32_t baseVertexLocation)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);

    uint32_t const indexBufferSid = pDXContext->svgaDXContext.inputAssembly.indexBufferSid;
    if (indexBufferSid == SVGA3D_INVALID_ID)
        return;
    uint32_t const indexBufferFormat = pDXContext->svgaDXContext.inputAssembly.indexBufferFormat;
    uint32_t const indexBufferOffset = pDXContext->svgaDXContext.inputAssembly.indexBufferOffset;

    PVMSVGA3DSURFACE pSurface = NULL;
    int rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, indexBufferSid, &pSurface);
    AssertReturnVoid(RT_SUCCESS(rc));
    AssertReturnVoid(pSurface->pBackendSurface->u.pBuffer && pSurface->pBackendSurface->enmResType == VMSVGA3D_RESTYPE_BUFFER);

    UINT const BytesPerIndex = indexBufferFormat == SVGA3D_R16_UINT ? 2 : 4;

    void *pvIndexBuffer;
    uint32_t cbIndexBuffer;
    rc = dxReadBuffer(pDXDevice, pSurface->pBackendSurface->u.pBuffer,
                      indexBufferOffset + startIndexLocation * BytesPerIndex,
                      indexCount * BytesPerIndex, &pvIndexBuffer, &cbIndexBuffer);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        uint8_t const *pu8IndexData = (uint8_t *)pvIndexBuffer;

        for (uint32_t iSlot = 0; iSlot < SVGA3D_DX_MAX_VERTEXBUFFERS; ++iSlot)
        {
            SVGA3dBufferBinding const *pVBInfo = &pDXContext->svgaDXContext.inputAssembly.vertexBuffers[iSlot];
            if (pVBInfo->bufferId == SVGA3D_INVALID_ID)
                continue;

            pSurface = NULL;
            rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, pVBInfo->bufferId, &pSurface);
            AssertContinue(RT_SUCCESS(rc));
            AssertContinue(pSurface->pBackendSurface->u.pBuffer && pSurface->pBackendSurface->enmResType == VMSVGA3D_RESTYPE_BUFFER);

            SVGA3dSurfaceImageId image;
            image.sid = pVBInfo->bufferId;
            image.face = 0;
            image.mipmap = 0;

            VMSVGA3D_MAPPED_SURFACE mapVB;
            rc = vmsvga3dBackSurfaceMap(pThisCC, &image, NULL, VMSVGA3D_SURFACE_MAP_READ, VMSVGA3D_MAP_F_NONE, &mapVB);
            AssertRC(rc);
            if (RT_SUCCESS(rc))
            {
                uint8_t const *pu8VertexData = (uint8_t *)mapVB.pvData;
                pu8VertexData += pVBInfo->offset;
                pu8VertexData += baseVertexLocation * (int32_t)pVBInfo->stride; /* signed */

                SVGA3dElementLayoutId const elementLayoutId = pDXContext->svgaDXContext.inputAssembly.layoutId;
                DXELEMENTLAYOUT *pDXElementLayout = &pDXContext->pBackendDXContext->paElementLayout[elementLayoutId];
                Assert(pDXElementLayout->cElementDesc > 0);

                Log8(("Vertex buffer dump: sid = %u, indexCount %u, startIndexLocation %d, baseVertexLocation %d, offset = %d, stride = %d:\n",
                      pVBInfo->bufferId, indexCount, startIndexLocation, baseVertexLocation, pVBInfo->offset, pVBInfo->stride));

                for (uint32_t i = 0; i < indexCount; ++i)
                {
                    uint32_t Index;
                    if (BytesPerIndex == 2)
                        Index = ((uint16_t *)pu8IndexData)[i];
                    else
                        Index = ((uint32_t *)pu8IndexData)[i];

                    Log8(("slot[%u] /* v%u */ { ", iSlot, Index));

                    for (uint32_t iElement = 0; iElement < pDXElementLayout->cElementDesc; ++iElement)
                    {
                        D3D11_INPUT_ELEMENT_DESC *pElement = &pDXElementLayout->aElementDesc[iElement];
                        if (pElement->InputSlotClass != D3D11_INPUT_PER_VERTEX_DATA)
                            continue;

                        if (pElement->InputSlot == iSlot)
                        {
                            uint8_t const *pu8Vertex = pu8VertexData + Index * pVBInfo->stride;
                            dxDbgLogVertexElement(pElement->Format, pu8Vertex + pElement->AlignedByteOffset);
                        }
                    }

                    Log8((" },\n"));

                    if (pVBInfo->stride == 0)
                        break;
                }

                vmsvga3dBackSurfaceUnmap(pThisCC, &image, &mapVB, /* fWritten =  */ false);
            }
        }

        RTMemFree(pvIndexBuffer);
    }
}


static void dxDbgDumpInstanceData(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, uint32_t instanceCount, uint32_t startInstanceLocation)
{
    /*
     * Dump per-instance data.
     */
    for (uint32_t iInstance = 0; iInstance < instanceCount; ++iInstance)
    {
        for (uint32_t iSlot = 0; iSlot < SVGA3D_DX_MAX_VERTEXBUFFERS; ++iSlot)
        {
            SVGA3dBufferBinding const *pVBInfo = &pDXContext->svgaDXContext.inputAssembly.vertexBuffers[iSlot];
            if (pVBInfo->bufferId == SVGA3D_INVALID_ID)
                continue;

            PVMSVGA3DSURFACE pSurface = NULL;
            int rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, pVBInfo->bufferId, &pSurface);
            AssertContinue(RT_SUCCESS(rc));
            AssertContinue(pSurface->pBackendSurface->u.pBuffer && pSurface->pBackendSurface->enmResType == VMSVGA3D_RESTYPE_BUFFER);

            SVGA3dSurfaceImageId image;
            image.sid = pVBInfo->bufferId;
            image.face = 0;
            image.mipmap = 0;

            VMSVGA3D_MAPPED_SURFACE mapVB;
            rc = vmsvga3dBackSurfaceMap(pThisCC, &image, NULL, VMSVGA3D_SURFACE_MAP_READ, VMSVGA3D_MAP_F_NONE, &mapVB);
            AssertRC(rc);
            if (RT_SUCCESS(rc))
            {
                uint8_t const *pu8VertexData = (uint8_t *)mapVB.pvData;
                pu8VertexData += pVBInfo->offset;
                pu8VertexData += startInstanceLocation * pVBInfo->stride;

                SVGA3dElementLayoutId const elementLayoutId = pDXContext->svgaDXContext.inputAssembly.layoutId;
                DXELEMENTLAYOUT *pDXElementLayout = &pDXContext->pBackendDXContext->paElementLayout[elementLayoutId];
                Assert(pDXElementLayout->cElementDesc > 0);

                Log8(("Instance data dump: sid = %u, iInstance %u, startInstanceLocation %d, offset = %d, stride = %d:\n",
                      pVBInfo->bufferId, iInstance, startInstanceLocation, pVBInfo->offset, pVBInfo->stride));

                Log8(("slot[%u] /* i%u */ { ", iSlot, iInstance));
                for (uint32_t iElement = 0; iElement < pDXElementLayout->cElementDesc; ++iElement)
                {
                    D3D11_INPUT_ELEMENT_DESC *pElement = &pDXElementLayout->aElementDesc[iElement];
                    if (pElement->InputSlotClass != D3D11_INPUT_PER_INSTANCE_DATA)
                        continue;

                    if (pElement->InputSlot == iSlot)
                    {
                        uint8_t const *pu8Vertex = pu8VertexData + iInstance * pVBInfo->stride;
                        dxDbgLogVertexElement(pElement->Format, pu8Vertex + pElement->AlignedByteOffset);
                    }
                }
                Log8((" },\n"));

                vmsvga3dBackSurfaceUnmap(pThisCC, &image, &mapVB, /* fWritten =  */ false);
            }
        }
    }
}

static void dxDbgDumpVertices_Draw(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, uint32_t vertexCount, uint32_t startVertexLocation)
{
    dxDbgDumpVertexData(pThisCC, pDXContext, vertexCount, startVertexLocation);
}


static void dxDbgDumpVertices_DrawIndexed(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, uint32_t indexCount, uint32_t startIndexLocation, int32_t baseVertexLocation)
{
    dxDbgDumpIndexedVertexData(pThisCC, pDXContext, indexCount, startIndexLocation, baseVertexLocation);
}


static void dxDbgDumpVertices_DrawInstanced(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext,
                                            uint32_t vertexCountPerInstance, uint32_t instanceCount,
                                            uint32_t startVertexLocation, uint32_t startInstanceLocation)
{
    dxDbgDumpVertexData(pThisCC, pDXContext, vertexCountPerInstance, startVertexLocation);
    dxDbgDumpInstanceData(pThisCC, pDXContext, instanceCount, startInstanceLocation);
}


static void dxDbgDumpVertices_DrawIndexedInstanced(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext,
                                                   uint32_t indexCountPerInstance, uint32_t instanceCount,
                                                   uint32_t startIndexLocation, int32_t baseVertexLocation,
                                                   uint32_t startInstanceLocation)
{
    dxDbgDumpIndexedVertexData(pThisCC, pDXContext, indexCountPerInstance, startIndexLocation, baseVertexLocation);
    dxDbgDumpInstanceData(pThisCC, pDXContext, instanceCount, startInstanceLocation);
}
#endif


static int dxCreateRenderTargetView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dRenderTargetViewId renderTargetViewId, SVGACOTableDXRTViewEntry const *pEntry)
{
    PVMSVGA3DSURFACE pSurface;
    ID3D11Resource *pResource;
    int rc = dxEnsureResource(pThisCC, pEntry->sid, &pSurface, &pResource);
    AssertRCReturn(rc, rc);

    DXVIEW *pView = &pDXContext->pBackendDXContext->paRenderTargetView[renderTargetViewId];
    Assert(pView->u.pView == NULL);

    ID3D11RenderTargetView *pRenderTargetView;
    HRESULT hr = dxRenderTargetViewCreate(pThisCC, pEntry, pSurface, &pRenderTargetView);
    AssertReturn(SUCCEEDED(hr), VERR_INVALID_STATE);

    return dxViewInit(pView, pSurface, pDXContext, renderTargetViewId, VMSVGA3D_VIEWTYPE_RENDERTARGET, pRenderTargetView);
}


static int dxEnsureRenderTargetView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dRenderTargetViewId viewId, DXVIEW **ppResult)
{
    ASSERT_GUEST_RETURN(viewId < pDXContext->cot.cRTView, VERR_INVALID_PARAMETER);

    DXVIEW *pDXView = &pDXContext->pBackendDXContext->paRenderTargetView[viewId];
    if (!pDXView->u.pView)
    {
        SVGACOTableDXRTViewEntry const *pEntry = &pDXContext->cot.paRTView[viewId];
        int rc = dxCreateRenderTargetView(pThisCC, pDXContext, viewId, pEntry);
        AssertRCReturn(rc, rc);
    }
    *ppResult = pDXView;
    return VINF_SUCCESS;
}


static int dxCreateDepthStencilView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dDepthStencilViewId depthStencilViewId, SVGACOTableDXDSViewEntry const *pEntry)
{
    PVMSVGA3DSURFACE pSurface;
    ID3D11Resource *pResource;
    int rc = dxEnsureResource(pThisCC, pEntry->sid, &pSurface, &pResource);
    AssertRCReturn(rc, rc);

    DXVIEW *pView = &pDXContext->pBackendDXContext->paDepthStencilView[depthStencilViewId];
    Assert(pView->u.pView == NULL);

    ID3D11DepthStencilView *pDepthStencilView;
    HRESULT hr = dxDepthStencilViewCreate(pThisCC, pEntry, pSurface, &pDepthStencilView);
    AssertReturn(SUCCEEDED(hr), VERR_INVALID_STATE);

    return dxViewInit(pView, pSurface, pDXContext, depthStencilViewId, VMSVGA3D_VIEWTYPE_DEPTHSTENCIL, pDepthStencilView);
}


static int dxEnsureDepthStencilView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dDepthStencilViewId viewId, DXVIEW **ppResult)
{
    ASSERT_GUEST_RETURN(viewId < pDXContext->cot.cDSView, VERR_INVALID_PARAMETER);

    DXVIEW *pDXView = &pDXContext->pBackendDXContext->paDepthStencilView[viewId];
    if (!pDXView->u.pView)
    {
        SVGACOTableDXDSViewEntry const *pEntry = &pDXContext->cot.paDSView[viewId];
        int rc = dxCreateDepthStencilView(pThisCC, pDXContext, viewId, pEntry);
        AssertRCReturn(rc, rc);
    }
    *ppResult = pDXView;
    return VINF_SUCCESS;
}


static int dxCreateShaderResourceView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dShaderResourceViewId shaderResourceViewId, SVGACOTableDXSRViewEntry const *pEntry)
{
    PVMSVGA3DSURFACE pSurface;
    ID3D11Resource *pResource;
    int rc = dxEnsureResource(pThisCC, pEntry->sid, &pSurface, &pResource);
    AssertRCReturn(rc, rc);

    DXVIEW *pView = &pDXContext->pBackendDXContext->paShaderResourceView[shaderResourceViewId];
    Assert(pView->u.pView == NULL);

    ID3D11ShaderResourceView *pShaderResourceView;
    HRESULT hr = dxShaderResourceViewCreate(pThisCC, pEntry, pSurface, &pShaderResourceView);
    AssertReturn(SUCCEEDED(hr), VERR_INVALID_STATE);

    return dxViewInit(pView, pSurface, pDXContext, shaderResourceViewId, VMSVGA3D_VIEWTYPE_SHADERRESOURCE, pShaderResourceView);
}


static int dxEnsureShaderResourceView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dShaderResourceViewId viewId, DXVIEW **ppResult)
{
    ASSERT_GUEST_RETURN(viewId < pDXContext->cot.cSRView, VERR_INVALID_PARAMETER);

    DXVIEW *pDXView = &pDXContext->pBackendDXContext->paShaderResourceView[viewId];
    if (!pDXView->u.pView)
    {
        SVGACOTableDXSRViewEntry const *pEntry = &pDXContext->cot.paSRView[viewId];
        int rc = dxCreateShaderResourceView(pThisCC, pDXContext, viewId, pEntry);
        AssertRCReturn(rc, rc);
    }
    *ppResult = pDXView;
    return VINF_SUCCESS;
}


static int dxCreateUnorderedAccessView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dUAViewId uaViewId, SVGACOTableDXUAViewEntry const *pEntry)
{
    PVMSVGA3DSURFACE pSurface;
    ID3D11Resource *pResource;
    int rc = dxEnsureResource(pThisCC, pEntry->sid, &pSurface, &pResource);
    AssertRCReturn(rc, rc);

    DXVIEW *pView = &pDXContext->pBackendDXContext->paUnorderedAccessView[uaViewId];
    Assert(pView->u.pView == NULL);

    ID3D11UnorderedAccessView *pUnorderedAccessView;
    HRESULT hr = dxUnorderedAccessViewCreate(pThisCC, pEntry, pSurface, &pUnorderedAccessView);
    AssertReturn(SUCCEEDED(hr), VERR_INVALID_STATE);

    return dxViewInit(pView, pSurface, pDXContext, uaViewId, VMSVGA3D_VIEWTYPE_UNORDEREDACCESS, pUnorderedAccessView);
}


static int dxEnsureUnorderedAccessView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dUAViewId viewId, DXVIEW **ppResult)
{
    ASSERT_GUEST_RETURN(viewId < pDXContext->cot.cUAView, VERR_INVALID_PARAMETER);

    DXVIEW *pDXView = &pDXContext->pBackendDXContext->paUnorderedAccessView[viewId];
    if (!pDXView->u.pView)
    {
        SVGACOTableDXUAViewEntry const *pEntry = &pDXContext->cot.paUAView[viewId];
        int rc = dxCreateUnorderedAccessView(pThisCC, pDXContext, viewId, pEntry);
        AssertRCReturn(rc, rc);
    }
    *ppResult = pDXView;
    return VINF_SUCCESS;
}


static void dxSetupPipeline(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    /* Make sure that any draw operations on shader resource views have finished. */
    AssertCompile(RT_ELEMENTS(pDXContext->svgaDXContext.shaderState) == SVGA3D_NUM_SHADERTYPE);
    AssertCompile(RT_ELEMENTS(pDXContext->svgaDXContext.shaderState[0].shaderResources) == SVGA3D_DX_MAX_SRVIEWS);

    int rc;

    /* Unbind render target views because they mught be (re-)used as shader resource views. */
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    pDXDevice->pImmediateContext->OMSetRenderTargetsAndUnorderedAccessViews(0, NULL, NULL, 0, 0, NULL, NULL);
    uint32_t const cMaxUAViews = pDXDevice->FeatureLevel >= D3D_FEATURE_LEVEL_11_1
                               ? SVGA3D_DX11_1_MAX_UAVIEWS
                               : SVGA3D_MAX_UAVIEWS;
    for (uint32_t i = 0; i < cMaxUAViews; ++i)
    {
        ID3D11UnorderedAccessView *pNullUA = 0;
        pDXDevice->pImmediateContext->CSSetUnorderedAccessViews(i, 1, &pNullUA, NULL);
    }

    dxSetConstantBuffers(pThisCC, pDXContext);
    dxSetVertexBuffers(pThisCC, pDXContext);
    dxSetIndexBuffer(pThisCC, pDXContext);

    /*
     * Shader resources
     */

    /* Make sure that the shader resource views exist. */
    for (uint32_t idxShaderState = 0; idxShaderState < SVGA3D_NUM_SHADERTYPE; ++idxShaderState)
    {
        for (uint32_t idxSR = 0; idxSR < SVGA3D_DX_MAX_SRVIEWS; ++idxSR)
        {
            SVGA3dShaderResourceViewId const shaderResourceViewId = pDXContext->svgaDXContext.shaderState[idxShaderState].shaderResources[idxSR];
            if (shaderResourceViewId != SVGA3D_INVALID_ID)
            {
                DXVIEW *pDXView;
                rc = dxEnsureShaderResourceView(pThisCC, pDXContext, shaderResourceViewId, &pDXView);
                AssertContinue(RT_SUCCESS(rc));

#ifdef LOG_ENABLED
                SVGACOTableDXSRViewEntry const *pSRViewEntry = &pDXContext->cot.paSRView[shaderResourceViewId];
                PVMSVGA3DSURFACE pSurface = NULL;
                vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, pDXView->sid, &pSurface);
                LogFunc(("srv[%d][%d] sid = %u, srvid = %u, format = %s(%d), %dx%d\n",
                         idxShaderState, idxSR, pDXView->sid, shaderResourceViewId, vmsvgaLookupEnum((int)pSRViewEntry->format, &g_SVGA3dSurfaceFormat2String), pSRViewEntry->format,
                         pSurface->paMipmapLevels[0].cBlocksX * pSurface->cxBlock, pSurface->paMipmapLevels[0].cBlocksY * pSurface->cyBlock));
#endif

#ifdef DUMP_BITMAPS
                SVGA3dSurfaceImageId image;
                image.sid = pDXView->sid;
                image.face = 0;
                image.mipmap = 0;
                VMSVGA3D_MAPPED_SURFACE map;
                int rc2 = vmsvga3dSurfaceMap(pThisCC, &image, NULL, VMSVGA3D_SURFACE_MAP_READ, VMSVGA3D_MAP_F_NONE, &map);
                if (RT_SUCCESS(rc2))
                {
                    vmsvga3dMapWriteBmpFile(&map, "sr-");
                    vmsvga3dSurfaceUnmap(pThisCC, &image, &map, /* fWritten =  */ false);
                }
                else
                    LogFunc(("Map failed %Rrc\n", rc));
#endif
            }
        }

        dxSetShaderResources(pThisCC, pDXContext, (SVGA3dShaderType)(idxShaderState + SVGA3D_SHADERTYPE_MIN));
    }

    /*
     * Compute shader unordered access views
     */

    for (uint32_t idxUA = 0; idxUA < cMaxUAViews; ++idxUA)
    {
        SVGA3dUAViewId const viewId = pDXContext->svgaDXContext.csuaViewIds[idxUA];
        if (viewId != SVGA3D_INVALID_ID)
        {
            DXVIEW *pDXView;
            rc = dxEnsureUnorderedAccessView(pThisCC, pDXContext, viewId, &pDXView);
            AssertContinue(RT_SUCCESS(rc));

#ifdef LOG_ENABLED
            SVGACOTableDXUAViewEntry const *pUAViewEntry = &pDXContext->cot.paUAView[viewId];
            LogFunc(("csuav[%d] sid = %u, uaid = %u, format = %s(%d)\n", idxUA, pDXView->sid, viewId, vmsvgaLookupEnum((int)pUAViewEntry->format, &g_SVGA3dSurfaceFormat2String), pUAViewEntry->format));
#endif
        }
    }

    /* Set views. */
    rc = dxSetCSUnorderedAccessViews(pThisCC, pDXContext);
    AssertRC(rc);

    /*
     * Render targets and unordered access views.
     */

    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturnVoid(pDevice->pDevice);

    /* Make sure that the render target views exist. */
    if (pDXContext->svgaDXContext.renderState.depthStencilViewId != SVGA3D_INVALID_ID)
    {
        uint32_t const viewId = pDXContext->svgaDXContext.renderState.depthStencilViewId;

        DXVIEW *pDXView;
        rc = dxEnsureDepthStencilView(pThisCC, pDXContext, viewId, &pDXView);
        AssertRC(rc);

#ifdef LOG_ENABLED
        SVGACOTableDXDSViewEntry const *pDSViewEntry = &pDXContext->cot.paDSView[viewId];
        LogFunc(("dsv sid = %u, dsvid = %u, format = %s(%d)\n", pDXView->sid, viewId, vmsvgaLookupEnum((int)pDSViewEntry->format, &g_SVGA3dSurfaceFormat2String), pDSViewEntry->format));
#endif
    }

    for (uint32_t i = 0; i < SVGA3D_MAX_SIMULTANEOUS_RENDER_TARGETS; ++i)
    {
        uint32_t const viewId = pDXContext->svgaDXContext.renderState.renderTargetViewIds[i];
        if (viewId != SVGA3D_INVALID_ID)
        {
            DXVIEW *pDXView;
            rc = dxEnsureRenderTargetView(pThisCC, pDXContext, viewId, &pDXView);
            AssertContinue(RT_SUCCESS(rc));

#ifdef LOG_ENABLED
            SVGACOTableDXRTViewEntry const *pRTViewEntry = &pDXContext->cot.paRTView[viewId];
            LogFunc(("rtv sid = %u, rtvid = %u, format = %s(%d)\n", pDXView->sid, viewId, vmsvgaLookupEnum((int)pRTViewEntry->format, &g_SVGA3dSurfaceFormat2String), pRTViewEntry->format));
#endif
        }
    }

    for (uint32_t idxUA = 0; idxUA < cMaxUAViews; ++idxUA)
    {
        SVGA3dUAViewId const viewId = pDXContext->svgaDXContext.uaViewIds[idxUA];
        if (viewId != SVGA3D_INVALID_ID)
        {
            DXVIEW *pDXView;
            rc = dxEnsureUnorderedAccessView(pThisCC, pDXContext, viewId, &pDXView);
            AssertContinue(RT_SUCCESS(rc));

#ifdef LOG_ENABLED
            SVGACOTableDXUAViewEntry const *pUAViewEntry = &pDXContext->cot.paUAView[viewId];
            LogFunc(("uav[%d] sid = %u, uaid = %u, format = %s(%d)\n", idxUA, pDXView->sid, viewId, vmsvgaLookupEnum((int)pUAViewEntry->format, &g_SVGA3dSurfaceFormat2String), pUAViewEntry->format));
#endif
        }
    }

    /* Set render targets. */
    rc = dxSetRenderTargets(pThisCC, pDXContext);
    AssertRC(rc);

    /*
     * Shaders
     */

    for (uint32_t idxShaderState = 0; idxShaderState < SVGA3D_NUM_SHADERTYPE; ++idxShaderState)
    {
        DXSHADER *pDXShader;
        SVGA3dShaderType const shaderType = (SVGA3dShaderType)(idxShaderState + SVGA3D_SHADERTYPE_MIN);
        SVGA3dShaderId const shaderId = pDXContext->svgaDXContext.shaderState[idxShaderState].shaderId;

        if (shaderId != SVGA3D_INVALID_ID)
        {
            pDXShader = &pDXContext->pBackendDXContext->paShader[shaderId];
            if (pDXShader->pShader == NULL)
            {
                /* Create a new shader. */

                /* Apply resource types to a pixel shader. */
                if (shaderType == SVGA3D_SHADERTYPE_PS) /* Others too? */
                {
                    VGPU10_RESOURCE_DIMENSION aResourceDimension[SVGA3D_DX_MAX_SRVIEWS];
                    RT_ZERO(aResourceDimension);
                    VGPU10_RESOURCE_RETURN_TYPE aResourceReturnType[SVGA3D_DX_MAX_SRVIEWS];
                    RT_ZERO(aResourceReturnType);
                    uint32_t cResources = 0;

                    for (uint32_t idxSR = 0; idxSR < SVGA3D_DX_MAX_SRVIEWS; ++idxSR)
                    {
                        SVGA3dShaderResourceViewId const shaderResourceViewId = pDXContext->svgaDXContext.shaderState[idxShaderState].shaderResources[idxSR];
                        if (shaderResourceViewId != SVGA3D_INVALID_ID)
                        {
                            ASSERT_GUEST_CONTINUE(shaderResourceViewId < pDXContext->cot.cSRView);
                            SVGACOTableDXSRViewEntry const *pSRViewEntry = &pDXContext->cot.paSRView[shaderResourceViewId];

                            PVMSVGA3DSURFACE pSurface;
                            rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, pSRViewEntry->sid, &pSurface);
                            AssertRCReturnVoid(rc);

                            aResourceReturnType[idxSR] = DXShaderResourceReturnTypeFromFormat(pSRViewEntry->format);

                            switch (pSRViewEntry->resourceDimension)
                            {
                                case SVGA3D_RESOURCE_BUFFEREX:
                                case SVGA3D_RESOURCE_BUFFER:
                                    aResourceDimension[idxSR] = VGPU10_RESOURCE_DIMENSION_BUFFER;
                                    break;
                                case SVGA3D_RESOURCE_TEXTURE1D:
                                    if (pSurface->surfaceDesc.numArrayElements <= 1)
                                        aResourceDimension[idxSR] = VGPU10_RESOURCE_DIMENSION_TEXTURE1D;
                                    else
                                        aResourceDimension[idxSR] = VGPU10_RESOURCE_DIMENSION_TEXTURE1DARRAY;
                                    break;
                                case SVGA3D_RESOURCE_TEXTURE2D:
                                    if (pSurface->surfaceDesc.numArrayElements <= 1)
                                        aResourceDimension[idxSR] = pSurface->surfaceDesc.multisampleCount > 1
                                                                  ? VGPU10_RESOURCE_DIMENSION_TEXTURE2DMS
                                                                  : VGPU10_RESOURCE_DIMENSION_TEXTURE2D;
                                    else
                                        aResourceDimension[idxSR] = pSurface->surfaceDesc.multisampleCount > 1
                                                                  ? VGPU10_RESOURCE_DIMENSION_TEXTURE2DMSARRAY
                                                                  : VGPU10_RESOURCE_DIMENSION_TEXTURE2DARRAY;
                                    break;
                                case SVGA3D_RESOURCE_TEXTURE3D:
                                    aResourceDimension[idxSR] = VGPU10_RESOURCE_DIMENSION_TEXTURE3D;
                                    break;
                                case SVGA3D_RESOURCE_TEXTURECUBE:
                                    if (pSurface->surfaceDesc.numArrayElements <= 6)
                                        aResourceDimension[idxSR] = VGPU10_RESOURCE_DIMENSION_TEXTURECUBE;
                                    else
                                        aResourceDimension[idxSR] = VGPU10_RESOURCE_DIMENSION_TEXTURECUBEARRAY;
                                    break;
                                default:
                                    ASSERT_GUEST_FAILED();
                                    aResourceDimension[idxSR] = VGPU10_RESOURCE_DIMENSION_TEXTURE2D;
                            }

                            cResources = idxSR + 1;
                        }
                    }

                    rc = DXShaderUpdateResources(&pDXShader->shaderInfo, aResourceDimension, aResourceReturnType, cResources);
                    AssertRC(rc); /* Ignore rc because the shader will most likely work anyway. */

                    /* Update componentType of the pixel shader output signature to correspond to the bound resources. */
                    for (uint32_t idxRT = 0;
                        idxRT < RT_MIN(SVGA3D_MAX_SIMULTANEOUS_RENDER_TARGETS, pDXShader->shaderInfo.cOutputSignature);
                        ++idxRT)
                    {
                        SVGA3dRenderTargetViewId const renderTargetViewId = pDXContext->svgaDXContext.renderState.renderTargetViewIds[idxRT];
                        if (renderTargetViewId != SVGA3D_INVALID_ID)
                        {
                            ASSERT_GUEST_CONTINUE(renderTargetViewId < pDXContext->cot.cRTView);
                            SVGACOTableDXRTViewEntry const *pRTViewEntry = &pDXContext->cot.paRTView[renderTargetViewId];

                            SVGA3dDXSignatureEntry *pSignatureEntry = &pDXShader->shaderInfo.aOutputSignature[idxRT];
                            pSignatureEntry->componentType = DXShaderComponentTypeFromFormat(pRTViewEntry->format);
                        }
                    }
                }

                if (shaderType == SVGA3D_SHADERTYPE_VS)
                {
                    /* Update componentType of the vertex shader input signature to correspond to the input declaration. */
                    vboxDXUpdateVSInputSignature(pDXContext, pDXShader);
                }

                vboxDXMatchShaderSignatures(pThisCC, pDXContext, pDXShader);

                rc = DXShaderCreateDXBC(&pDXShader->shaderInfo, &pDXShader->pvDXBC, &pDXShader->cbDXBC);
                if (RT_SUCCESS(rc))
                {
#ifdef LOG_ENABLED
                    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
                    if (pBackend->pfnD3DDisassemble && LogIs6Enabled())
                    {
                        ID3D10Blob *pBlob = 0;
                        HRESULT hr2 = pBackend->pfnD3DDisassemble(pDXShader->pvDXBC, pDXShader->cbDXBC, 0, NULL, &pBlob);
                        if (SUCCEEDED(hr2) && pBlob && pBlob->GetBufferSize())
                            Log6(("%s\n", pBlob->GetBufferPointer()));
                        else
                            AssertFailed();
                        D3D_RELEASE(pBlob);
                    }
                    LogFunc(("Shader: set cid=%u shid=%u type=%d, GuestSignatures %d\n", pDXContext->cid, shaderId, pDXShader->enmShaderType, pDXShader->shaderInfo.fGuestSignatures));
#endif

                    HRESULT hr = dxShaderCreate(pThisCC, pDXContext, pDXShader);
                    if (FAILED(hr))
                        rc = VERR_INVALID_STATE;
                }
            }

            LogFunc(("Shader: cid=%u shid=%u type=%d, GuestSignatures %d, %Rrc\n", pDXContext->cid, shaderId, pDXShader->enmShaderType, pDXShader->shaderInfo.fGuestSignatures, rc));
        }
        else
            pDXShader = NULL;

        if (RT_SUCCESS(rc))
            dxShaderSet(pThisCC, pDXContext, shaderType, pDXShader);

        AssertRC(rc);
    }

    /*
     * InputLayout
     */
    SVGA3dElementLayoutId const elementLayoutId = pDXContext->svgaDXContext.inputAssembly.layoutId;
    ID3D11InputLayout *pInputLayout = NULL;
    if (elementLayoutId != SVGA3D_INVALID_ID)
    {
        DXELEMENTLAYOUT *pDXElementLayout = &pDXContext->pBackendDXContext->paElementLayout[elementLayoutId];
        if (!pDXElementLayout->pElementLayout)
        {
            uint32_t const idxShaderState = SVGA3D_SHADERTYPE_VS - SVGA3D_SHADERTYPE_MIN;
            uint32_t const shid = pDXContext->svgaDXContext.shaderState[idxShaderState].shaderId;
            if (shid < pDXContext->pBackendDXContext->cShader)
            {
                DXSHADER *pDXShader = &pDXContext->pBackendDXContext->paShader[shid];
                if (pDXShader->pvDXBC)
                    dxCreateInputLayout(pThisCC, pDXContext, elementLayoutId, pDXShader);
                else
                    LogRelMax(16, ("VMSVGA: DX shader bytecode is not available in DXSetInputLayout: shid = %u\n", shid));
            }
            else
                LogRelMax(16, ("VMSVGA: DX shader is not set in DXSetInputLayout: shid = 0x%x\n", shid));
        }

        pInputLayout = pDXElementLayout->pElementLayout;

        LogFunc(("Input layout id %u\n", elementLayoutId));
    }

    pDevice->pImmediateContext->IASetInputLayout(pInputLayout);

    LogFunc(("Topology %u\n", pDXContext->svgaDXContext.inputAssembly.topology));
    LogFunc(("Blend id %u\n", pDXContext->svgaDXContext.renderState.blendStateId));
}


static DECLCALLBACK(int) vmsvga3dBackDXDraw(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, uint32_t vertexCount, uint32_t startVertexLocation)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    dxSetupPipeline(pThisCC, pDXContext);

#ifdef LOG_ENABLED
    if (LogIs8Enabled())
        dxDbgDumpVertices_Draw(pThisCC, pDXContext, vertexCount, startVertexLocation);
#endif

    if (pDXContext->svgaDXContext.inputAssembly.topology != SVGA3D_PRIMITIVE_TRIANGLEFAN)
        pDevice->pImmediateContext->Draw(vertexCount, startVertexLocation);
    else
    {
        /*
         * Emulate SVGA3D_PRIMITIVE_TRIANGLEFAN using an indexed draw of a triangle list.
         */

        /* Make sure that 16 bit indices are enough. */
        if (vertexCount > 65535)
        {
            LogRelMax(1, ("VMSVGA: ignore Draw(TRIANGLEFAN, %u)\n", vertexCount));
            return VERR_NOT_SUPPORTED;
        }

        /* Generate indices. */
        UINT const IndexCount = 3 * (vertexCount - 2); /* 3_per_triangle * num_triangles */
        UINT const cbAlloc = IndexCount * sizeof(USHORT);
        USHORT *paIndices = (USHORT *)RTMemAlloc(cbAlloc);
        AssertReturn(paIndices, VERR_NO_MEMORY);
        USHORT iVertex = 1;
        for (UINT i = 0; i < IndexCount; i+= 3)
        {
            paIndices[i] = 0;
            paIndices[i + 1] = iVertex;
            ++iVertex;
            paIndices[i + 2] = iVertex;
        }

        D3D11_SUBRESOURCE_DATA InitData;
        InitData.pSysMem          = paIndices;
        InitData.SysMemPitch      = cbAlloc;
        InitData.SysMemSlicePitch = cbAlloc;

        D3D11_BUFFER_DESC bd;
        RT_ZERO(bd);
        bd.ByteWidth           = cbAlloc;
        bd.Usage               = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags           = D3D11_BIND_INDEX_BUFFER;
        //bd.CPUAccessFlags      = 0;
        //bd.MiscFlags           = 0;
        //bd.StructureByteStride = 0;

        ID3D11Buffer *pIndexBuffer = 0;
        HRESULT hr = pDevice->pDevice->CreateBuffer(&bd, &InitData, &pIndexBuffer);
        Assert(SUCCEEDED(hr));RT_NOREF(hr);

        /* Save the current index buffer. */
        ID3D11Buffer *pSavedIndexBuffer = 0;
        DXGI_FORMAT  SavedFormat = DXGI_FORMAT_UNKNOWN;
        UINT         SavedOffset = 0;
        pDevice->pImmediateContext->IAGetIndexBuffer(&pSavedIndexBuffer, &SavedFormat, &SavedOffset);

        /* Set up the device state. */
        pDevice->pImmediateContext->IASetIndexBuffer(pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
        pDevice->pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        UINT const StartIndexLocation = 0;
        INT const BaseVertexLocation = startVertexLocation;
        pDevice->pImmediateContext->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);

        /* Restore the device state. */
        pDevice->pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        pDevice->pImmediateContext->IASetIndexBuffer(pSavedIndexBuffer, SavedFormat, SavedOffset);
        D3D_RELEASE(pSavedIndexBuffer);

        /* Cleanup. */
        D3D_RELEASE(pIndexBuffer);
        RTMemFree(paIndices);
    }

#ifdef DX_FLUSH_AFTER_DRAW
    dxDeviceFlush(pDevice);
#endif

    return VINF_SUCCESS;
}

static int dxReadBuffer(DXDEVICE *pDevice, ID3D11Buffer *pBuffer, UINT Offset, UINT Bytes, void **ppvData, uint32_t *pcbData)
{
    D3D11_BUFFER_DESC desc;
    RT_ZERO(desc);
    pBuffer->GetDesc(&desc);

    AssertReturn(   Offset < desc.ByteWidth
                 && Bytes <= desc.ByteWidth - Offset, VERR_INVALID_STATE);

    void *pvData = RTMemAlloc(Bytes);
    if (!pvData)
        return VERR_NO_MEMORY;

    *ppvData = pvData;
    *pcbData = Bytes;

    D3D11_SUBRESOURCE_DATA *pInitialData = NULL;
    D3D11_BUFFER_DESC bd;
    RT_ZERO(bd);
    bd.ByteWidth           = Bytes;
    bd.Usage               = D3D11_USAGE_STAGING;
    //bd.BindFlags         = 0; /* No bind flags are allowed for staging resources. */
    bd.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ;

    int rc = VINF_SUCCESS;
    ID3D11Buffer *pStagingBuffer;
    HRESULT hr = pDevice->pDevice->CreateBuffer(&bd, pInitialData, &pStagingBuffer);
    if (SUCCEEDED(hr))
    {
        /* Copy from the buffer to the staging buffer. */
        ID3D11Resource *pDstResource = pStagingBuffer;
        UINT DstSubresource = 0;
        UINT DstX = 0;
        UINT DstY = 0;
        UINT DstZ = 0;
        ID3D11Resource *pSrcResource = pBuffer;
        UINT SrcSubresource = 0;
        D3D11_BOX SrcBox;
        SrcBox.left   = Offset;
        SrcBox.top    = 0;
        SrcBox.front  = 0;
        SrcBox.right  = Offset + Bytes;
        SrcBox.bottom = 1;
        SrcBox.back   = 1;
        pDevice->pImmediateContext->CopySubresourceRegion(pDstResource, DstSubresource, DstX, DstY, DstZ,
                                                          pSrcResource, SrcSubresource, &SrcBox);

        D3D11_MAPPED_SUBRESOURCE mappedResource;
        UINT const Subresource = 0; /* Buffers have only one subresource. */
        hr = pDevice->pImmediateContext->Map(pStagingBuffer, Subresource,
                                             D3D11_MAP_READ, /* MapFlags =  */ 0, &mappedResource);
        if (SUCCEEDED(hr))
        {
            memcpy(pvData, mappedResource.pData, Bytes);

            /* Unmap the staging buffer. */
            pDevice->pImmediateContext->Unmap(pStagingBuffer, Subresource);
        }
        else
            AssertFailedStmt(rc = VERR_NOT_SUPPORTED);

        D3D_RELEASE(pStagingBuffer);
    }
    else
    {
        rc = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(rc))
    {
        RTMemFree(*ppvData);
        *ppvData = NULL;
        *pcbData = 0;
    }

    return rc;
}


static int dxDrawIndexedTriangleFan(DXDEVICE *pDevice, uint32_t IndexCountTF, uint32_t StartIndexLocationTF, int32_t BaseVertexLocationTF)
{
    /*
     * Emulate an indexed SVGA3D_PRIMITIVE_TRIANGLEFAN using an indexed draw of triangle list.
     */

    /* Make sure that 16 bit indices are enough. */
    if (IndexCountTF > 65535)
    {
        LogRelMax(1, ("VMSVGA: ignore DrawIndexed(TRIANGLEFAN, %u)\n", IndexCountTF));
        return VERR_NOT_SUPPORTED;
    }

    /* Save the current index buffer. */
    ID3D11Buffer *pSavedIndexBuffer = 0;
    DXGI_FORMAT  SavedFormat = DXGI_FORMAT_UNKNOWN;
    UINT         SavedOffset = 0;
    pDevice->pImmediateContext->IAGetIndexBuffer(&pSavedIndexBuffer, &SavedFormat, &SavedOffset);

    AssertReturn(   SavedFormat == DXGI_FORMAT_R16_UINT
                 || SavedFormat == DXGI_FORMAT_R32_UINT, VERR_NOT_SUPPORTED);

    /* How many bytes are used by triangle fan indices. */
    UINT const BytesPerIndexTF = SavedFormat == DXGI_FORMAT_R16_UINT ? 2 : 4;
    UINT const BytesTF = BytesPerIndexTF * IndexCountTF;

    /* Read the current index buffer content to obtain indices. */
    void *pvDataTF;
    uint32_t cbDataTF;
    int rc = dxReadBuffer(pDevice, pSavedIndexBuffer, StartIndexLocationTF, BytesTF, &pvDataTF, &cbDataTF);
    AssertRCReturn(rc, rc);
    AssertReturnStmt(cbDataTF >= BytesPerIndexTF, RTMemFree(pvDataTF), VERR_INVALID_STATE);

    /* Generate indices for triangle list. */
    UINT const IndexCount = 3 * (IndexCountTF - 2); /* 3_per_triangle * num_triangles */
    UINT const cbAlloc = IndexCount * sizeof(USHORT);
    USHORT *paIndices = (USHORT *)RTMemAlloc(cbAlloc);
    AssertReturnStmt(paIndices, RTMemFree(pvDataTF), VERR_NO_MEMORY);

    USHORT iVertex = 1;
    if (BytesPerIndexTF == 2)
    {
        USHORT *paIndicesTF = (USHORT *)pvDataTF;
        for (UINT i = 0; i < IndexCount; i+= 3)
        {
            paIndices[i] = paIndicesTF[0];
            AssertBreakStmt(iVertex < IndexCountTF, rc = VERR_INVALID_STATE);
            paIndices[i + 1] = paIndicesTF[iVertex];
            ++iVertex;
            AssertBreakStmt(iVertex < IndexCountTF, rc = VERR_INVALID_STATE);
            paIndices[i + 2] = paIndicesTF[iVertex];
        }
    }
    else
    {
        UINT *paIndicesTF = (UINT *)pvDataTF;
        for (UINT i = 0; i < IndexCount; i+= 3)
        {
            paIndices[i] = paIndicesTF[0];
            AssertBreakStmt(iVertex < IndexCountTF, rc = VERR_INVALID_STATE);
            paIndices[i + 1] = paIndicesTF[iVertex];
            ++iVertex;
            AssertBreakStmt(iVertex < IndexCountTF, rc = VERR_INVALID_STATE);
            paIndices[i + 2] = paIndicesTF[iVertex];
        }
    }

    D3D11_SUBRESOURCE_DATA InitData;
    InitData.pSysMem          = paIndices;
    InitData.SysMemPitch      = cbAlloc;
    InitData.SysMemSlicePitch = cbAlloc;

    D3D11_BUFFER_DESC bd;
    RT_ZERO(bd);
    bd.ByteWidth           = cbAlloc;
    bd.Usage               = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags           = D3D11_BIND_INDEX_BUFFER;
    //bd.CPUAccessFlags      = 0;
    //bd.MiscFlags           = 0;
    //bd.StructureByteStride = 0;

    ID3D11Buffer *pIndexBuffer = 0;
    HRESULT hr = pDevice->pDevice->CreateBuffer(&bd, &InitData, &pIndexBuffer);
    Assert(SUCCEEDED(hr));RT_NOREF(hr);

    /* Set up the device state. */
    pDevice->pImmediateContext->IASetIndexBuffer(pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    pDevice->pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    UINT const StartIndexLocation = 0;
    INT const BaseVertexLocation = BaseVertexLocationTF;
    pDevice->pImmediateContext->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);

    /* Restore the device state. */
    pDevice->pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    pDevice->pImmediateContext->IASetIndexBuffer(pSavedIndexBuffer, SavedFormat, SavedOffset);
    D3D_RELEASE(pSavedIndexBuffer);

    /* Cleanup. */
    D3D_RELEASE(pIndexBuffer);
    RTMemFree(paIndices);
    RTMemFree(pvDataTF);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXDrawIndexed(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, uint32_t indexCount, uint32_t startIndexLocation, int32_t baseVertexLocation)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    dxSetupPipeline(pThisCC, pDXContext);

#ifdef LOG_ENABLED
    if (LogIs8Enabled())
        dxDbgDumpVertices_DrawIndexed(pThisCC, pDXContext, indexCount, startIndexLocation, baseVertexLocation);
#endif

    if (pDXContext->svgaDXContext.inputAssembly.topology != SVGA3D_PRIMITIVE_TRIANGLEFAN)
        pDevice->pImmediateContext->DrawIndexed(indexCount, startIndexLocation, baseVertexLocation);
    else
    {
        dxDrawIndexedTriangleFan(pDevice, indexCount, startIndexLocation, baseVertexLocation);
    }

#ifdef DX_FLUSH_AFTER_DRAW
    dxDeviceFlush(pDevice);
#endif

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXDrawInstanced(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext,
                                                     uint32_t vertexCountPerInstance, uint32_t instanceCount, uint32_t startVertexLocation, uint32_t startInstanceLocation)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    dxSetupPipeline(pThisCC, pDXContext);

#ifdef LOG_ENABLED
    if (LogIs8Enabled())
        dxDbgDumpVertices_DrawInstanced(pThisCC, pDXContext, vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
#endif

    Assert(pDXContext->svgaDXContext.inputAssembly.topology != SVGA3D_PRIMITIVE_TRIANGLEFAN);

    pDevice->pImmediateContext->DrawInstanced(vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);

#ifdef DX_FLUSH_AFTER_DRAW
    dxDeviceFlush(pDevice);
#endif

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXDrawIndexedInstanced(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext,
                                                            uint32_t indexCountPerInstance, uint32_t instanceCount, uint32_t startIndexLocation, int32_t baseVertexLocation, uint32_t startInstanceLocation)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    dxSetupPipeline(pThisCC, pDXContext);

#ifdef LOG_ENABLED
    if (LogIs8Enabled())
        dxDbgDumpVertices_DrawIndexedInstanced(pThisCC, pDXContext, indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation);
#endif

    Assert(pDXContext->svgaDXContext.inputAssembly.topology != SVGA3D_PRIMITIVE_TRIANGLEFAN);

    pDevice->pImmediateContext->DrawIndexedInstanced(indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation);

#ifdef DX_FLUSH_AFTER_DRAW
    dxDeviceFlush(pDevice);
#endif

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXDrawAuto(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    dxSetupPipeline(pThisCC, pDXContext);

    Assert(pDXContext->svgaDXContext.inputAssembly.topology != SVGA3D_PRIMITIVE_TRIANGLEFAN);

    pDevice->pImmediateContext->DrawAuto();

#ifdef DX_FLUSH_AFTER_DRAW
    dxDeviceFlush(pDevice);
#endif

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXSetInputLayout(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dElementLayoutId elementLayoutId)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend, pDXContext);

    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    RT_NOREF(elementLayoutId);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXSetVertexBuffers(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, uint32_t startBuffer, uint32_t cVertexBuffer, SVGA3dVertexBuffer const *paVertexBuffer)
{
    /* Will be set in setupPipeline. */
    RT_NOREF(pThisCC, pDXContext, startBuffer, cVertexBuffer, paVertexBuffer);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXSetIndexBuffer(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dSurfaceId sid, SVGA3dSurfaceFormat format, uint32_t offset)
{
    /* Will be set in setupPipeline. */
    RT_NOREF(pThisCC, pDXContext, sid, format, offset);
    return VINF_SUCCESS;
}

static D3D11_PRIMITIVE_TOPOLOGY dxTopology(SVGA3dPrimitiveType primitiveType)
{
    ASSERT_GUEST_RETURN(primitiveType < SVGA3D_PRIMITIVE_MAX, D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);

    static D3D11_PRIMITIVE_TOPOLOGY const aD3D11PrimitiveTopology[SVGA3D_PRIMITIVE_MAX] =
    {
        D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED,
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
        D3D11_PRIMITIVE_TOPOLOGY_POINTLIST,
        D3D11_PRIMITIVE_TOPOLOGY_LINELIST,
        D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP,
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, /* SVGA3D_PRIMITIVE_TRIANGLEFAN: No FAN in D3D11. */
        D3D11_PRIMITIVE_TOPOLOGY_LINELIST_ADJ,
        D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ,
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ,
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ,
        D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_5_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_6_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_7_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_8_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_9_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_10_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_11_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_12_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_13_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_14_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_15_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_16_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_17_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_18_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_19_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_20_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_21_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_22_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_23_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_24_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_25_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_26_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_27_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_28_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_29_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_30_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_31_CONTROL_POINT_PATCHLIST,
        D3D11_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST,
    };
    return aD3D11PrimitiveTopology[primitiveType];
}

static DECLCALLBACK(int) vmsvga3dBackDXSetTopology(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dPrimitiveType topology)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend, pDXContext);

    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    D3D11_PRIMITIVE_TOPOLOGY const enmTopology = dxTopology(topology);
    pDevice->pImmediateContext->IASetPrimitiveTopology(enmTopology);
    return VINF_SUCCESS;
}


static int dxSetRenderTargets(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    UINT UAVStartSlot = 0;
    UINT NumUAVs = 0;
    ID3D11UnorderedAccessView *apUnorderedAccessViews[SVGA3D_DX11_1_MAX_UAVIEWS];
    UINT aUAVInitialCounts[SVGA3D_DX11_1_MAX_UAVIEWS];
    uint32_t const cMaxUAViews = pDevice->FeatureLevel >= D3D_FEATURE_LEVEL_11_1
                               ? SVGA3D_DX11_1_MAX_UAVIEWS
                               : SVGA3D_MAX_UAVIEWS;
    for (uint32_t idxUA = 0; idxUA < cMaxUAViews; ++idxUA)
    {
        apUnorderedAccessViews[idxUA] =  NULL;
        aUAVInitialCounts[idxUA] = (UINT)-1;

        SVGA3dUAViewId const uaViewId = pDXContext->svgaDXContext.uaViewIds[idxUA];
        if (uaViewId != SVGA3D_INVALID_ID)
        {
            ASSERT_GUEST_CONTINUE(uaViewId < pDXContext->cot.cUAView);

            if (NumUAVs == 0)
                UAVStartSlot = idxUA;
            NumUAVs = idxUA - UAVStartSlot + 1;
            apUnorderedAccessViews[idxUA] = pDXContext->pBackendDXContext->paUnorderedAccessView[uaViewId].u.pUnorderedAccessView;

            SVGACOTableDXUAViewEntry const *pEntry = &pDXContext->cot.paUAView[uaViewId];
            aUAVInitialCounts[idxUA] = pEntry->structureCount;
        }
    }

    UINT NumRTVs = 0;
    ID3D11RenderTargetView *apRenderTargetViews[SVGA3D_MAX_RENDER_TARGETS];
    RT_ZERO(apRenderTargetViews);
    for (uint32_t i = 0; i < pDXContext->cRenderTargets; ++i)
    {
        SVGA3dRenderTargetViewId const renderTargetViewId = pDXContext->svgaDXContext.renderState.renderTargetViewIds[i];
        if (renderTargetViewId != SVGA3D_INVALID_ID)
        {
            ASSERT_GUEST_RETURN(renderTargetViewId < pDXContext->pBackendDXContext->cRenderTargetView, VERR_INVALID_PARAMETER);
            apRenderTargetViews[i] = pDXContext->pBackendDXContext->paRenderTargetView[renderTargetViewId].u.pRenderTargetView;
            ++NumRTVs;
        }
    }

    /* RTVs are followed by UAVs. */
    Assert(NumUAVs == 0 || NumRTVs <= pDXContext->svgaDXContext.uavSpliceIndex);

    ID3D11DepthStencilView *pDepthStencilView = NULL;
    SVGA3dDepthStencilViewId const depthStencilViewId = pDXContext->svgaDXContext.renderState.depthStencilViewId;
    if (depthStencilViewId != SVGA_ID_INVALID)
        pDepthStencilView = pDXContext->pBackendDXContext->paDepthStencilView[depthStencilViewId].u.pDepthStencilView;

    pDevice->pImmediateContext->OMSetRenderTargetsAndUnorderedAccessViews(NumRTVs,
                                                   apRenderTargetViews,
                                                   pDepthStencilView,
                                                   pDXContext->svgaDXContext.uavSpliceIndex,
                                                   NumUAVs,
                                                   apUnorderedAccessViews,
                                                   aUAVInitialCounts);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXSetRenderTargets(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dDepthStencilViewId depthStencilViewId, uint32_t cRenderTargetViewId, SVGA3dRenderTargetViewId const *paRenderTargetViewId)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend, pDXContext);

    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    RT_NOREF(depthStencilViewId, cRenderTargetViewId, paRenderTargetViewId);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXSetBlendState(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dBlendStateId blendId, float const blendFactor[4], uint32_t sampleMask)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    if (blendId != SVGA3D_INVALID_ID)
    {
        ID3D11BlendState1 *pBlendState = pDXContext->pBackendDXContext->papBlendState[blendId];
        pDevice->pImmediateContext->OMSetBlendState(pBlendState, blendFactor, sampleMask);
    }
    else
        pDevice->pImmediateContext->OMSetBlendState(NULL, NULL, 0);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXSetDepthStencilState(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dDepthStencilStateId depthStencilId, uint32_t stencilRef)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    if (depthStencilId != SVGA3D_INVALID_ID)
    {
        ID3D11DepthStencilState *pDepthStencilState = pDXContext->pBackendDXContext->papDepthStencilState[depthStencilId];
        pDevice->pImmediateContext->OMSetDepthStencilState(pDepthStencilState, stencilRef);
    }
    else
        pDevice->pImmediateContext->OMSetDepthStencilState(NULL, 0);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXSetRasterizerState(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dRasterizerStateId rasterizerId)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    RT_NOREF(pBackend);

    if (rasterizerId != SVGA3D_INVALID_ID)
    {
        ID3D11RasterizerState1 *pRasterizerState = pDXContext->pBackendDXContext->papRasterizerState[rasterizerId];
        pDevice->pImmediateContext->RSSetState(pRasterizerState);
    }
    else
        pDevice->pImmediateContext->RSSetState(NULL);

    return VINF_SUCCESS;
}


typedef struct VGPU10QUERYINFO
{
    SVGA3dQueryType svgaQueryType;
    uint32_t        cbDataVMSVGA;
    D3D11_QUERY     dxQueryType;
    uint32_t        cbDataD3D11;
} VGPU10QUERYINFO;

static VGPU10QUERYINFO const *dxQueryInfo(SVGA3dQueryType type)
{
    static VGPU10QUERYINFO const aQueryInfo[SVGA3D_QUERYTYPE_MAX] =
    {
        { SVGA3D_QUERYTYPE_OCCLUSION,                sizeof(SVGADXOcclusionQueryResult),
          D3D11_QUERY_OCCLUSION,                     sizeof(UINT64) },
        { SVGA3D_QUERYTYPE_TIMESTAMP,                sizeof(SVGADXTimestampQueryResult),
          D3D11_QUERY_TIMESTAMP,                     sizeof(UINT64) },
        { SVGA3D_QUERYTYPE_TIMESTAMPDISJOINT,        sizeof(SVGADXTimestampDisjointQueryResult),
          D3D11_QUERY_TIMESTAMP_DISJOINT,            sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT) },
        { SVGA3D_QUERYTYPE_PIPELINESTATS,            sizeof(SVGADXPipelineStatisticsQueryResult),
          D3D11_QUERY_PIPELINE_STATISTICS,           sizeof(D3D11_QUERY_DATA_PIPELINE_STATISTICS) },
        { SVGA3D_QUERYTYPE_OCCLUSIONPREDICATE,       sizeof(SVGADXOcclusionPredicateQueryResult),
          D3D11_QUERY_OCCLUSION_PREDICATE,           sizeof(BOOL) },
        { SVGA3D_QUERYTYPE_STREAMOUTPUTSTATS,        sizeof(SVGADXStreamOutStatisticsQueryResult),
          D3D11_QUERY_SO_STATISTICS,                 sizeof(D3D11_QUERY_DATA_SO_STATISTICS) },
        { SVGA3D_QUERYTYPE_STREAMOVERFLOWPREDICATE,  sizeof(SVGADXStreamOutPredicateQueryResult),
          D3D11_QUERY_SO_OVERFLOW_PREDICATE,         sizeof(BOOL) },
        { SVGA3D_QUERYTYPE_OCCLUSION64,              sizeof(SVGADXOcclusion64QueryResult),
          D3D11_QUERY_OCCLUSION,                     sizeof(UINT64) },
        { SVGA3D_QUERYTYPE_SOSTATS_STREAM0,          sizeof(SVGADXStreamOutStatisticsQueryResult),
          D3D11_QUERY_SO_STATISTICS_STREAM0,         sizeof(D3D11_QUERY_DATA_SO_STATISTICS) },
        { SVGA3D_QUERYTYPE_SOSTATS_STREAM1,          sizeof(SVGADXStreamOutStatisticsQueryResult),
          D3D11_QUERY_SO_STATISTICS_STREAM1,         sizeof(D3D11_QUERY_DATA_SO_STATISTICS) },
        { SVGA3D_QUERYTYPE_SOSTATS_STREAM2,          sizeof(SVGADXStreamOutStatisticsQueryResult),
          D3D11_QUERY_SO_STATISTICS_STREAM2,         sizeof(D3D11_QUERY_DATA_SO_STATISTICS) },
        { SVGA3D_QUERYTYPE_SOSTATS_STREAM3,          sizeof(SVGADXStreamOutStatisticsQueryResult),
          D3D11_QUERY_SO_STATISTICS_STREAM3,         sizeof(D3D11_QUERY_DATA_SO_STATISTICS) },
        { SVGA3D_QUERYTYPE_SOP_STREAM0,              sizeof(SVGADXStreamOutPredicateQueryResult),
          D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM0, sizeof(BOOL) },
        { SVGA3D_QUERYTYPE_SOP_STREAM1,              sizeof(SVGADXStreamOutPredicateQueryResult),
          D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM1, sizeof(BOOL) },
        { SVGA3D_QUERYTYPE_SOP_STREAM2,              sizeof(SVGADXStreamOutPredicateQueryResult),
          D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM2, sizeof(BOOL) },
        { SVGA3D_QUERYTYPE_SOP_STREAM3,              sizeof(SVGADXStreamOutPredicateQueryResult),
          D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM3, sizeof(BOOL) },
    };

    ASSERT_GUEST_RETURN(type < RT_ELEMENTS(aQueryInfo), NULL);
    return &aQueryInfo[type];
}

static int dxDefineQuery(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dQueryId queryId, SVGACOTableDXQueryEntry const *pEntry)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pDevice, VERR_INVALID_STATE);

    DXQUERY *pDXQuery = &pDXContext->pBackendDXContext->paQuery[queryId];
    VGPU10QUERYINFO const *pQueryInfo = dxQueryInfo((SVGA3dQueryType)pEntry->type);
    if (!pQueryInfo)
        return VERR_INVALID_PARAMETER;

    D3D11_QUERY_DESC desc;
    desc.Query     = pQueryInfo->dxQueryType;
    desc.MiscFlags = 0;
    if (pEntry->flags & SVGA3D_DXQUERY_FLAG_PREDICATEHINT)
        desc.MiscFlags |= (UINT)D3D11_QUERY_MISC_PREDICATEHINT;

    HRESULT hr = pDXDevice->pDevice->CreateQuery(&desc, &pDXQuery->pQuery);
    AssertReturn(SUCCEEDED(hr), VERR_INVALID_STATE);

    return VINF_SUCCESS;
}


static int dxDestroyQuery(DXQUERY *pDXQuery)
{
    D3D_RELEASE(pDXQuery->pQuery);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXDefineQuery(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dQueryId queryId, SVGACOTableDXQueryEntry const *pEntry)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    return dxDefineQuery(pThisCC, pDXContext, queryId, pEntry);
}


static DECLCALLBACK(int) vmsvga3dBackDXDestroyQuery(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dQueryId queryId)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXQUERY *pDXQuery = &pDXContext->pBackendDXContext->paQuery[queryId];
    dxDestroyQuery(pDXQuery);

    return VINF_SUCCESS;
}


/** @todo queryId makes pDXQuery redundant */
static int dxBeginQuery(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dQueryId queryId, DXQUERY *pDXQuery)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pDevice, VERR_INVALID_STATE);

    /* Begin is disabled for some queries. */
    SVGACOTableDXQueryEntry *pEntry = &pDXContext->cot.paQuery[queryId];
    if (pEntry->type == SVGA3D_QUERYTYPE_TIMESTAMP)
        return VINF_SUCCESS;

    pDXDevice->pImmediateContext->Begin(pDXQuery->pQuery);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXBeginQuery(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dQueryId queryId)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXQUERY *pDXQuery = &pDXContext->pBackendDXContext->paQuery[queryId];
    int rc = dxBeginQuery(pThisCC, pDXContext, queryId, pDXQuery);
    return rc;
}


static int dxGetQueryResult(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dQueryId queryId,
                            SVGADXQueryResultUnion *pQueryResult, uint32_t *pcbOut)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pDevice, VERR_INVALID_STATE);

    typedef union _DXQUERYRESULT
    {
        UINT64                               occlusion;
        UINT64                               timestamp;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT  timestampDisjoint;
        D3D11_QUERY_DATA_PIPELINE_STATISTICS pipelineStatistics;
        BOOL                                 occlusionPredicate;
        D3D11_QUERY_DATA_SO_STATISTICS       soStatistics;
        BOOL                                 soOverflowPredicate;
    } DXQUERYRESULT;

    DXQUERY *pDXQuery = &pDXContext->pBackendDXContext->paQuery[queryId];
    SVGACOTableDXQueryEntry *pEntry = &pDXContext->cot.paQuery[queryId];
    VGPU10QUERYINFO const *pQueryInfo = dxQueryInfo((SVGA3dQueryType)pEntry->type);
    if (!pQueryInfo)
        return VERR_INVALID_PARAMETER;

    DXQUERYRESULT dxQueryResult;
    while (pDXDevice->pImmediateContext->GetData(pDXQuery->pQuery, &dxQueryResult, pQueryInfo->cbDataD3D11, 0) != S_OK)
    {
        RTThreadYield();
    }

    /* Copy back the result. */
    switch (pEntry->type)
    {
        case SVGA3D_QUERYTYPE_OCCLUSION:
            pQueryResult->occ.samplesRendered = (uint32_t)dxQueryResult.occlusion;
            break;
        case SVGA3D_QUERYTYPE_TIMESTAMP:
            pQueryResult->ts.timestamp = dxQueryResult.timestamp;
            break;
        case SVGA3D_QUERYTYPE_TIMESTAMPDISJOINT:
            pQueryResult->tsDisjoint.realFrequency = dxQueryResult.timestampDisjoint.Frequency;
            pQueryResult->tsDisjoint.disjoint = dxQueryResult.timestampDisjoint.Disjoint;
            break;
        case SVGA3D_QUERYTYPE_PIPELINESTATS:
            pQueryResult->pipelineStats.inputAssemblyVertices     = dxQueryResult.pipelineStatistics.IAVertices;
            pQueryResult->pipelineStats.inputAssemblyPrimitives   = dxQueryResult.pipelineStatistics.IAPrimitives;
            pQueryResult->pipelineStats.vertexShaderInvocations   = dxQueryResult.pipelineStatistics.VSInvocations;
            pQueryResult->pipelineStats.geometryShaderInvocations = dxQueryResult.pipelineStatistics.GSInvocations;
            pQueryResult->pipelineStats.geometryShaderPrimitives  = dxQueryResult.pipelineStatistics.GSPrimitives;
            pQueryResult->pipelineStats.clipperInvocations        = dxQueryResult.pipelineStatistics.CInvocations;
            pQueryResult->pipelineStats.clipperPrimitives         = dxQueryResult.pipelineStatistics.CPrimitives;
            pQueryResult->pipelineStats.pixelShaderInvocations    = dxQueryResult.pipelineStatistics.PSInvocations;
            pQueryResult->pipelineStats.hullShaderInvocations     = dxQueryResult.pipelineStatistics.HSInvocations;
            pQueryResult->pipelineStats.domainShaderInvocations   = dxQueryResult.pipelineStatistics.DSInvocations;
            pQueryResult->pipelineStats.computeShaderInvocations  = dxQueryResult.pipelineStatistics.CSInvocations;
            break;
        case SVGA3D_QUERYTYPE_OCCLUSIONPREDICATE:
            pQueryResult->occPred.anySamplesRendered = dxQueryResult.occlusionPredicate;
            break;
        case SVGA3D_QUERYTYPE_STREAMOUTPUTSTATS:
        case SVGA3D_QUERYTYPE_SOSTATS_STREAM0:
        case SVGA3D_QUERYTYPE_SOSTATS_STREAM1:
        case SVGA3D_QUERYTYPE_SOSTATS_STREAM2:
        case SVGA3D_QUERYTYPE_SOSTATS_STREAM3:
            pQueryResult->soStats.numPrimitivesWritten  = dxQueryResult.soStatistics.NumPrimitivesWritten;
            pQueryResult->soStats.numPrimitivesRequired = dxQueryResult.soStatistics.PrimitivesStorageNeeded;
            break;
        case SVGA3D_QUERYTYPE_STREAMOVERFLOWPREDICATE:
        case SVGA3D_QUERYTYPE_SOP_STREAM0:
        case SVGA3D_QUERYTYPE_SOP_STREAM1:
        case SVGA3D_QUERYTYPE_SOP_STREAM2:
        case SVGA3D_QUERYTYPE_SOP_STREAM3:
            pQueryResult->soPred.overflowed = dxQueryResult.soOverflowPredicate;
            break;
        case SVGA3D_QUERYTYPE_OCCLUSION64:
            pQueryResult->occ64.samplesRendered = dxQueryResult.occlusion;
            break;
    }

    *pcbOut = pQueryInfo->cbDataVMSVGA;
    return VINF_SUCCESS;
}

static int dxEndQuery(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dQueryId queryId,
                      SVGADXQueryResultUnion *pQueryResult, uint32_t *pcbOut)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pDevice, VERR_INVALID_STATE);

    DXQUERY *pDXQuery = &pDXContext->pBackendDXContext->paQuery[queryId];
    pDXDevice->pImmediateContext->End(pDXQuery->pQuery);

    /** @todo Consider issuing QueryEnd and getting data later in FIFO thread loop. */
    return dxGetQueryResult(pThisCC, pDXContext, queryId, pQueryResult, pcbOut);
}


static DECLCALLBACK(int) vmsvga3dBackDXEndQuery(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext,
                                                SVGA3dQueryId queryId, SVGADXQueryResultUnion *pQueryResult, uint32_t *pcbOut)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    int rc = dxEndQuery(pThisCC, pDXContext, queryId, pQueryResult, pcbOut);
    return rc;
}


static DECLCALLBACK(int) vmsvga3dBackDXSetPredication(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dQueryId queryId, uint32_t predicateValue)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pDevice, VERR_INVALID_STATE);

    if (queryId != SVGA3D_INVALID_ID)
    {
        DEBUG_BREAKPOINT_TEST();
        DXQUERY *pDXQuery = &pDXContext->pBackendDXContext->paQuery[queryId];
        SVGACOTableDXQueryEntry *pEntry = &pDXContext->cot.paQuery[queryId];

        VGPU10QUERYINFO const *pQueryInfo = dxQueryInfo((SVGA3dQueryType)pEntry->type);
        if (!pQueryInfo)
            return VERR_INVALID_PARAMETER;

        D3D_RELEASE(pDXQuery->pQuery);

        D3D11_QUERY_DESC desc;
        desc.Query     = pQueryInfo->dxQueryType;
        desc.MiscFlags = 0;
        if (pEntry->flags & SVGA3D_DXQUERY_FLAG_PREDICATEHINT)
            desc.MiscFlags |= (UINT)D3D11_QUERY_MISC_PREDICATEHINT;

        HRESULT hr = pDXDevice->pDevice->CreatePredicate(&desc, &pDXQuery->pPredicate);
        AssertReturn(SUCCEEDED(hr), VERR_INVALID_STATE);

        pDXDevice->pImmediateContext->SetPredication(pDXQuery->pPredicate, RT_BOOL(predicateValue));
    }
    else
        pDXDevice->pImmediateContext->SetPredication(NULL, FALSE);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXSetSOTargets(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, uint32_t cSOTarget, SVGA3dSoTarget const *paSoTarget)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    /* For each paSoTarget[i]:
     *   If the stream outout buffer object does not exist then create it.
     *   If the surface has been updated by the guest then update the buffer object.
     * Use SOSetTargets to set the buffers.
     */

    ID3D11Buffer *paResource[SVGA3D_DX_MAX_SOTARGETS];
    UINT paOffset[SVGA3D_DX_MAX_SOTARGETS];

    /* Always re-bind all 4 SO targets. They can be NULL. */
    for (uint32_t i = 0; i < SVGA3D_DX_MAX_SOTARGETS; ++i)
    {
        /* Get corresponding resource. Create the buffer if does not yet exist. */
        if (i < cSOTarget && paSoTarget[i].sid != SVGA_ID_INVALID)
        {
            PVMSVGA3DSURFACE pSurface;
            int rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, paSoTarget[i].sid, &pSurface);
            AssertRCReturn(rc, rc);

            if (pSurface->pBackendSurface == NULL)
            {
                /* Create the resource. */
                rc = vmsvga3dBackSurfaceCreateSoBuffer(pThisCC, pSurface);
                AssertRCReturn(rc, rc);
            }

            /** @todo How paSoTarget[i].sizeInBytes is used? Maybe when the buffer is created? */
            paResource[i] = pSurface->pBackendSurface->u.pBuffer;
            paOffset[i] = paSoTarget[i].offset;
        }
        else
        {
            paResource[i] = NULL;
            paOffset[i] = 0;
        }
    }

    pDevice->pImmediateContext->SOSetTargets(SVGA3D_DX_MAX_SOTARGETS, paResource, paOffset);

    pDXContext->pBackendDXContext->cSOTarget = cSOTarget;

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXSetViewports(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, uint32_t cViewport, SVGA3dViewport const *paViewport)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    RT_NOREF(pBackend, pDXContext);

    /* D3D11_VIEWPORT is identical to SVGA3dViewport. */
    D3D11_VIEWPORT *pViewports = (D3D11_VIEWPORT *)paViewport;

    pDevice->pImmediateContext->RSSetViewports(cViewport, pViewports);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXSetScissorRects(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, uint32_t cRect, SVGASignedRect const *paRect)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend, pDXContext);

    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    /* D3D11_RECT is identical to SVGASignedRect. */
    D3D11_RECT *pRects = (D3D11_RECT *)paRect;

    pDevice->pImmediateContext->RSSetScissorRects(cRect, pRects);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXClearRenderTargetView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dRenderTargetViewId renderTargetViewId, SVGA3dRGBAFloat const *pRGBA)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pDevice, VERR_INVALID_STATE);

    DXVIEW *pDXView;
    int rc = dxEnsureRenderTargetView(pThisCC, pDXContext, renderTargetViewId, &pDXView);
    AssertRCReturn(rc, rc);

    pDXDevice->pImmediateContext->ClearRenderTargetView(pDXView->u.pRenderTargetView, pRGBA->value);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXClearRenderTargetViewRegion(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dRenderTargetViewId renderTargetViewId,
                                                                     SVGA3dRGBAFloat const *pColor, uint32_t cRect, SVGASignedRect const *paRect)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pDevice, VERR_INVALID_STATE);

    DXVIEW *pDXView;
    int rc = dxEnsureRenderTargetView(pThisCC, pDXContext, renderTargetViewId, &pDXView);
    AssertRCReturn(rc, rc);

    pDXDevice->pImmediateContext->ClearView(pDXView->u.pRenderTargetView, pColor->value, (D3D11_RECT *)paRect, cRect);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXClearDepthStencilView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, uint32_t flags, SVGA3dDepthStencilViewId depthStencilViewId, float depth, uint8_t stencil)
{
    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    DXVIEW *pDXView;
    int rc = dxEnsureDepthStencilView(pThisCC, pDXContext, depthStencilViewId, &pDXView);
    AssertRCReturn(rc, rc);

    UINT ClearFlags = 0;
    if (flags & SVGA3D_CLEAR_DEPTH)
        ClearFlags |= D3D11_CLEAR_DEPTH;
    if (flags & SVGA3D_CLEAR_STENCIL)
        ClearFlags |= D3D11_CLEAR_STENCIL;

    pDevice->pImmediateContext->ClearDepthStencilView(pDXView->u.pDepthStencilView, ClearFlags, depth, stencil);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXPredCopyRegion(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dSurfaceId dstSid, uint32_t dstSubResource, SVGA3dSurfaceId srcSid, uint32_t srcSubResource, SVGA3dCopyBox const *pBox)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend, pDXContext);

    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    PVMSVGA3DSURFACE pSrcSurface;
    int rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, srcSid, &pSrcSurface);
    AssertRCReturn(rc, rc);

    PVMSVGA3DSURFACE pDstSurface;
    rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, dstSid, &pDstSurface);
    AssertRCReturn(rc, rc);

    if (pSrcSurface->pBackendSurface == NULL)
    {
        /* Create the resource. */
        rc = vmsvga3dBackSurfaceCreateResource(pThisCC, pSrcSurface);
        AssertRCReturn(rc, rc);
    }

    if (pDstSurface->pBackendSurface == NULL)
    {
        /* Create the resource. */
        rc = vmsvga3dBackSurfaceCreateResource(pThisCC, pDstSurface);
        AssertRCReturn(rc, rc);
    }

    LogFunc(("src%s sid = %u -> dst%s sid = %u\n",
             (pSrcSurface->f.surfaceFlags & SVGA3D_SURFACE_SCREENTARGET) ? " st" : "", pSrcSurface->id,
             (pDstSurface->f.surfaceFlags & SVGA3D_SURFACE_SCREENTARGET) ? " st" : "", pDstSurface->id));

    /* Clip the box. */
    /** @todo Use [src|dst]SubResource to index p[Src|Dst]Surface->paMipmapLevels array directly. */
    uint32_t iSrcFace;
    uint32_t iSrcMipmap;
    vmsvga3dCalcMipmapAndFace(pSrcSurface->cLevels, srcSubResource, &iSrcMipmap, &iSrcFace);

    uint32_t iDstFace;
    uint32_t iDstMipmap;
    vmsvga3dCalcMipmapAndFace(pDstSurface->cLevels, dstSubResource, &iDstMipmap, &iDstFace);

    PVMSVGA3DMIPMAPLEVEL pSrcMipLevel;
    rc = vmsvga3dMipmapLevel(pSrcSurface, iSrcFace, iSrcMipmap, &pSrcMipLevel);
    ASSERT_GUEST_RETURN(RT_SUCCESS(rc), rc);

    PVMSVGA3DMIPMAPLEVEL pDstMipLevel;
    rc = vmsvga3dMipmapLevel(pDstSurface, iDstFace, iDstMipmap, &pDstMipLevel);
    ASSERT_GUEST_RETURN(RT_SUCCESS(rc), rc);

    SVGA3dCopyBox clipBox = *pBox;
    vmsvgaR3ClipCopyBox(&pSrcMipLevel->mipmapSize, &pDstMipLevel->mipmapSize, &clipBox);

    UINT DstSubresource = dstSubResource;
    UINT DstX = clipBox.x;
    UINT DstY = clipBox.y;
    UINT DstZ = clipBox.z;

    UINT SrcSubresource = srcSubResource;
    D3D11_BOX SrcBox;
    SrcBox.left   = clipBox.srcx;
    SrcBox.top    = clipBox.srcy;
    SrcBox.front  = clipBox.srcz;
    SrcBox.right  = clipBox.srcx + clipBox.w;
    SrcBox.bottom = clipBox.srcy + clipBox.h;
    SrcBox.back   = clipBox.srcz + clipBox.d;

    ID3D11Resource *pDstResource;
    ID3D11Resource *pSrcResource;

    pDstResource = dxResource(pDstSurface);
    pSrcResource = dxResource(pSrcSurface);

    pDevice->pImmediateContext->CopySubresourceRegion(pDstResource, DstSubresource, DstX, DstY, DstZ,
                                                      pSrcResource, SrcSubresource, &SrcBox);

#ifdef DUMP_BITMAPS
    SVGA3dSurfaceImageId image;
    image.sid = pDstSurface->id;
    image.face = 0;
    image.mipmap = 0;
    VMSVGA3D_MAPPED_SURFACE map;
    int rc2 = vmsvga3dSurfaceMap(pThisCC, &image, NULL, VMSVGA3D_SURFACE_MAP_READ, VMSVGA3D_MAP_F_NONE, &map);
    if (RT_SUCCESS(rc2))
    {
        vmsvga3dMapWriteBmpFile(&map, "copyregion-");
        vmsvga3dSurfaceUnmap(pThisCC, &image, &map, /* fWritten =  */ false);
    }
    else
        Log(("Map failed %Rrc\n", rc));
#endif

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXPredCopy(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dSurfaceId dstSid, SVGA3dSurfaceId srcSid)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend, pDXContext);

    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    PVMSVGA3DSURFACE pSrcSurface;
    int rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, srcSid, &pSrcSurface);
    AssertRCReturn(rc, rc);

    PVMSVGA3DSURFACE pDstSurface;
    rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, dstSid, &pDstSurface);
    AssertRCReturn(rc, rc);

    if (pSrcSurface->pBackendSurface == NULL)
    {
        /* Create the resource. */
        rc = vmsvga3dBackSurfaceCreateResource(pThisCC, pSrcSurface);
        AssertRCReturn(rc, rc);
    }

    if (pDstSurface->pBackendSurface == NULL)
    {
        /* Create the resource. */
        rc = vmsvga3dBackSurfaceCreateResource(pThisCC, pDstSurface);
        AssertRCReturn(rc, rc);
    }

    LogFunc(("src%s sid = %u -> dst%s sid = %u\n",
             (pSrcSurface->f.surfaceFlags & SVGA3D_SURFACE_SCREENTARGET) ? " st" : "", pSrcSurface->id,
             (pDstSurface->f.surfaceFlags & SVGA3D_SURFACE_SCREENTARGET) ? " st" : "", pDstSurface->id));

    ID3D11Resource *pDstResource = dxResource(pDstSurface);
    ID3D11Resource *pSrcResource = dxResource(pSrcSurface);

    pDevice->pImmediateContext->CopyResource(pDstResource, pSrcResource);

    return VINF_SUCCESS;
}


#include "shaders/d3d11blitter.hlsl.vs.h"
#include "shaders/d3d11blitter.hlsl.ps.h"

#define HTEST(stmt) \
    hr = stmt; \
    AssertReturn(SUCCEEDED(hr), hr)


static void BlitRelease(D3D11BLITTER *pBlitter)
{
    D3D_RELEASE(pBlitter->pVertexShader);
    D3D_RELEASE(pBlitter->pPixelShader);
    D3D_RELEASE(pBlitter->pSamplerState);
    D3D_RELEASE(pBlitter->pRasterizerState);
    D3D_RELEASE(pBlitter->pBlendState);
    RT_ZERO(*pBlitter);
}


static HRESULT BlitInit(D3D11BLITTER *pBlitter, ID3D11Device1 *pDevice, ID3D11DeviceContext1 *pImmediateContext)
{
    HRESULT hr;

    RT_ZERO(*pBlitter);

    pBlitter->pDevice = pDevice;
    pBlitter->pImmediateContext = pImmediateContext;

    HTEST(pBlitter->pDevice->CreateVertexShader(g_vs_blitter, sizeof(g_vs_blitter), NULL, &pBlitter->pVertexShader));
    HTEST(pBlitter->pDevice->CreatePixelShader(g_ps_blitter, sizeof(g_ps_blitter), NULL, &pBlitter->pPixelShader));

    D3D11_SAMPLER_DESC SamplerDesc;
    SamplerDesc.Filter         = D3D11_FILTER_ANISOTROPIC;
    SamplerDesc.AddressU       = D3D11_TEXTURE_ADDRESS_WRAP;
    SamplerDesc.AddressV       = D3D11_TEXTURE_ADDRESS_WRAP;
    SamplerDesc.AddressW       = D3D11_TEXTURE_ADDRESS_WRAP;
    SamplerDesc.MipLODBias     = 0.0f;
    SamplerDesc.MaxAnisotropy  = 4;
    SamplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    SamplerDesc.BorderColor[0] = 0.0f;
    SamplerDesc.BorderColor[1] = 0.0f;
    SamplerDesc.BorderColor[2] = 0.0f;
    SamplerDesc.BorderColor[3] = 0.0f;
    SamplerDesc.MinLOD         = 0.0f;
    SamplerDesc.MaxLOD         = 0.0f;
    HTEST(pBlitter->pDevice->CreateSamplerState(&SamplerDesc, &pBlitter->pSamplerState));

    D3D11_RASTERIZER_DESC1 RasterizerDesc;
    RasterizerDesc.FillMode              = D3D11_FILL_SOLID;
    RasterizerDesc.CullMode              = D3D11_CULL_NONE;
    RasterizerDesc.FrontCounterClockwise = FALSE;
    RasterizerDesc.DepthBias             = 0;
    RasterizerDesc.DepthBiasClamp        = 0.0f;
    RasterizerDesc.SlopeScaledDepthBias  = 0.0f;
    RasterizerDesc.DepthClipEnable       = FALSE;
    RasterizerDesc.ScissorEnable         = FALSE;
    RasterizerDesc.MultisampleEnable     = FALSE;
    RasterizerDesc.AntialiasedLineEnable = FALSE;
    RasterizerDesc.ForcedSampleCount     = 0;
    HTEST(pBlitter->pDevice->CreateRasterizerState1(&RasterizerDesc, &pBlitter->pRasterizerState));

    D3D11_BLEND_DESC1 BlendDesc;
    BlendDesc.AlphaToCoverageEnable = FALSE;
    BlendDesc.IndependentBlendEnable = FALSE;
    for (unsigned i = 0; i < RT_ELEMENTS(BlendDesc.RenderTarget); ++i)
    {
        BlendDesc.RenderTarget[i].BlendEnable           = FALSE;
        BlendDesc.RenderTarget[i].LogicOpEnable         = FALSE;
        BlendDesc.RenderTarget[i].SrcBlend              = D3D11_BLEND_SRC_COLOR;
        BlendDesc.RenderTarget[i].DestBlend             = D3D11_BLEND_ZERO;
        BlendDesc.RenderTarget[i].BlendOp               = D3D11_BLEND_OP_ADD;
        BlendDesc.RenderTarget[i].SrcBlendAlpha         = D3D11_BLEND_SRC_ALPHA;
        BlendDesc.RenderTarget[i].DestBlendAlpha        = D3D11_BLEND_ZERO;
        BlendDesc.RenderTarget[i].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
        BlendDesc.RenderTarget[i].LogicOp               = D3D11_LOGIC_OP_CLEAR;
        BlendDesc.RenderTarget[i].RenderTargetWriteMask = 0xF;
    }
    HTEST(pBlitter->pDevice->CreateBlendState1(&BlendDesc, &pBlitter->pBlendState));

    return S_OK;
}


static HRESULT BlitFromTexture(D3D11BLITTER *pBlitter, ID3D11RenderTargetView *pDstRenderTargetView,
                               float cDstWidth, float cDstHeight, D3D11_RECT const &rectDst,
                               ID3D11ShaderResourceView *pSrcShaderResourceView)
{
    HRESULT hr;

    /*
     * Save pipeline state.
     */
    struct
    {
        D3D11_PRIMITIVE_TOPOLOGY    Topology;
        ID3D11InputLayout          *pInputLayout;
        ID3D11Buffer               *pConstantBuffer;
        ID3D11VertexShader         *pVertexShader;
        ID3D11HullShader           *pHullShader;
        ID3D11DomainShader         *pDomainShader;
        ID3D11GeometryShader       *pGeometryShader;
        ID3D11ShaderResourceView   *pShaderResourceView;
        ID3D11PixelShader          *pPixelShader;
        ID3D11SamplerState         *pSamplerState;
        ID3D11RasterizerState      *pRasterizerState;
        ID3D11BlendState           *pBlendState;
        FLOAT                       BlendFactor[4];
        UINT                        SampleMask;
        ID3D11RenderTargetView     *apRenderTargetView[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
        ID3D11DepthStencilView     *pDepthStencilView;
        UINT                        NumViewports;
        D3D11_VIEWPORT              aViewport[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    } SavedState;

    pBlitter->pImmediateContext->IAGetPrimitiveTopology(&SavedState.Topology);
    pBlitter->pImmediateContext->IAGetInputLayout(&SavedState.pInputLayout);
    pBlitter->pImmediateContext->VSGetConstantBuffers(0, 1, &SavedState.pConstantBuffer);
    pBlitter->pImmediateContext->VSGetShader(&SavedState.pVertexShader, NULL, NULL);
    pBlitter->pImmediateContext->HSGetShader(&SavedState.pHullShader, NULL, NULL);
    pBlitter->pImmediateContext->DSGetShader(&SavedState.pDomainShader, NULL, NULL);
    pBlitter->pImmediateContext->GSGetShader(&SavedState.pGeometryShader, NULL, NULL);
    pBlitter->pImmediateContext->PSGetShaderResources(0, 1, &SavedState.pShaderResourceView);
    pBlitter->pImmediateContext->PSGetShader(&SavedState.pPixelShader, NULL, NULL);
    pBlitter->pImmediateContext->PSGetSamplers(0, 1, &SavedState.pSamplerState);
    pBlitter->pImmediateContext->RSGetState(&SavedState.pRasterizerState);
    pBlitter->pImmediateContext->OMGetBlendState(&SavedState.pBlendState, SavedState.BlendFactor, &SavedState.SampleMask);
    pBlitter->pImmediateContext->OMGetRenderTargets(RT_ELEMENTS(SavedState.apRenderTargetView), SavedState.apRenderTargetView, &SavedState.pDepthStencilView);
    SavedState.NumViewports = RT_ELEMENTS(SavedState.aViewport);
    pBlitter->pImmediateContext->RSGetViewports(&SavedState.NumViewports, &SavedState.aViewport[0]);

    /*
     * Setup pipeline for the blitter.
     */

    /* Render target is first.
     * If the source texture is bound as a render target, then this call will unbind it
     * and allow to use it as the shader resource.
     */
    pBlitter->pImmediateContext->OMSetRenderTargets(1, &pDstRenderTargetView, NULL);

    /* Input assembler. */
    pBlitter->pImmediateContext->IASetInputLayout(NULL);
    pBlitter->pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    /* Constant buffer. */
    struct
    {
        float scaleX;
        float scaleY;
        float offsetX;
        float offsetY;
    } VSConstantBuffer;
    VSConstantBuffer.scaleX = (float)(rectDst.right - rectDst.left) / cDstWidth;
    VSConstantBuffer.scaleY = (float)(rectDst.bottom - rectDst.top) / cDstHeight;
    VSConstantBuffer.offsetX = (float)(rectDst.right + rectDst.left) / cDstWidth - 1.0f;
    VSConstantBuffer.offsetY = -((float)(rectDst.bottom + rectDst.top) / cDstHeight - 1.0f);

    D3D11_SUBRESOURCE_DATA initialData;
    initialData.pSysMem          = &VSConstantBuffer;
    initialData.SysMemPitch      = sizeof(VSConstantBuffer);
    initialData.SysMemSlicePitch = sizeof(VSConstantBuffer);

    D3D11_BUFFER_DESC bd;
    RT_ZERO(bd);
    bd.ByteWidth           = sizeof(VSConstantBuffer);
    bd.Usage               = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags           = D3D11_BIND_CONSTANT_BUFFER;

    ID3D11Buffer *pConstantBuffer;
    HTEST(pBlitter->pDevice->CreateBuffer(&bd, &initialData, &pConstantBuffer));
    pBlitter->pImmediateContext->VSSetConstantBuffers(0, 1, &pConstantBuffer);
    D3D_RELEASE(pConstantBuffer); /* xSSetConstantBuffers "will hold a reference to the interfaces passed in." */

    /* Vertex shader. */
    pBlitter->pImmediateContext->VSSetShader(pBlitter->pVertexShader, NULL, 0);

    /* Unused shaders. */
    pBlitter->pImmediateContext->HSSetShader(NULL, NULL, 0);
    pBlitter->pImmediateContext->DSSetShader(NULL, NULL, 0);
    pBlitter->pImmediateContext->GSSetShader(NULL, NULL, 0);

    /* Shader resource view. */
    pBlitter->pImmediateContext->PSSetShaderResources(0, 1, &pSrcShaderResourceView);

    /* Pixel shader. */
    pBlitter->pImmediateContext->PSSetShader(pBlitter->pPixelShader, NULL, 0);

    /* Sampler. */
    pBlitter->pImmediateContext->PSSetSamplers(0, 1, &pBlitter->pSamplerState);

    /* Rasterizer. */
    pBlitter->pImmediateContext->RSSetState(pBlitter->pRasterizerState);

    /* Blend state. */
    static FLOAT const BlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    pBlitter->pImmediateContext->OMSetBlendState(pBlitter->pBlendState, BlendFactor, 0xffffffff);

    /* Viewport. */
    D3D11_VIEWPORT Viewport;
    Viewport.TopLeftX = 0;
    Viewport.TopLeftY = 0;
    Viewport.Width    = cDstWidth;
    Viewport.Height   = cDstHeight;
    Viewport.MinDepth = 0.0f;
    Viewport.MaxDepth = 1.0f;
    pBlitter->pImmediateContext->RSSetViewports(1, &Viewport);

    /* Draw. */
    pBlitter->pImmediateContext->Draw(4, 0);

    /*
     * Restore pipeline state.
     */
    pBlitter->pImmediateContext->IASetPrimitiveTopology(SavedState.Topology);
    pBlitter->pImmediateContext->IASetInputLayout(SavedState.pInputLayout);
    D3D_RELEASE(SavedState.pInputLayout);
    pBlitter->pImmediateContext->VSSetConstantBuffers(0, 1, &SavedState.pConstantBuffer);
    D3D_RELEASE(SavedState.pConstantBuffer);
    pBlitter->pImmediateContext->VSSetShader(SavedState.pVertexShader, NULL, 0);
    D3D_RELEASE(SavedState.pVertexShader);

    pBlitter->pImmediateContext->HSSetShader(SavedState.pHullShader, NULL, 0);
    D3D_RELEASE(SavedState.pHullShader);
    pBlitter->pImmediateContext->DSSetShader(SavedState.pDomainShader, NULL, 0);
    D3D_RELEASE(SavedState.pDomainShader);
    pBlitter->pImmediateContext->GSSetShader(SavedState.pGeometryShader, NULL, 0);
    D3D_RELEASE(SavedState.pGeometryShader);

    pBlitter->pImmediateContext->PSSetShaderResources(0, 1, &SavedState.pShaderResourceView);
    D3D_RELEASE(SavedState.pShaderResourceView);
    pBlitter->pImmediateContext->PSSetShader(SavedState.pPixelShader, NULL, 0);
    D3D_RELEASE(SavedState.pPixelShader);
    pBlitter->pImmediateContext->PSSetSamplers(0, 1, &SavedState.pSamplerState);
    D3D_RELEASE(SavedState.pSamplerState);
    pBlitter->pImmediateContext->RSSetState(SavedState.pRasterizerState);
    D3D_RELEASE(SavedState.pRasterizerState);
    pBlitter->pImmediateContext->OMSetBlendState(SavedState.pBlendState, SavedState.BlendFactor, SavedState.SampleMask);
    D3D_RELEASE(SavedState.pBlendState);
    pBlitter->pImmediateContext->OMSetRenderTargets(RT_ELEMENTS(SavedState.apRenderTargetView), SavedState.apRenderTargetView, SavedState.pDepthStencilView);
    D3D_RELEASE_ARRAY(RT_ELEMENTS(SavedState.apRenderTargetView), SavedState.apRenderTargetView);
    D3D_RELEASE(SavedState.pDepthStencilView);
    pBlitter->pImmediateContext->RSSetViewports(SavedState.NumViewports, &SavedState.aViewport[0]);

    return S_OK;
}


static DECLCALLBACK(int) vmsvga3dBackDXPresentBlt(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext,
                                                  SVGA3dSurfaceId dstSid, uint32_t dstSubResource, SVGA3dBox const *pBoxDst,
                                                  SVGA3dSurfaceId srcSid, uint32_t srcSubResource, SVGA3dBox const *pBoxSrc,
                                                  SVGA3dDXPresentBltMode mode)
{
    RT_NOREF(pDXContext, mode);

    ASSERT_GUEST_RETURN(pBoxDst->z == 0 && pBoxDst->d == 1, VERR_INVALID_PARAMETER);
    ASSERT_GUEST_RETURN(pBoxSrc->z == 0 && pBoxSrc->d == 1, VERR_INVALID_PARAMETER);

    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    PVMSVGA3DSURFACE pSrcSurface;
    int rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, srcSid, &pSrcSurface);
    AssertRCReturn(rc, rc);

    PVMSVGA3DSURFACE pDstSurface;
    rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, dstSid, &pDstSurface);
    AssertRCReturn(rc, rc);

    if (pSrcSurface->pBackendSurface == NULL)
    {
        /* Create the resource. */
        rc = vmsvga3dBackSurfaceCreateResource(pThisCC, pSrcSurface);
        AssertRCReturn(rc, rc);
    }

    if (pDstSurface->pBackendSurface == NULL)
    {
        /* Create the resource. */
        rc = vmsvga3dBackSurfaceCreateResource(pThisCC, pDstSurface);
        AssertRCReturn(rc, rc);
    }

#ifdef DEBUG_sunlover
    if (pSrcSurface->surfaceDesc.multisampleCount > 1 || pDstSurface->surfaceDesc.multisampleCount > 1)
        DEBUG_BREAKPOINT_TEST();
#endif

    LogFunc(("src%s sid = %u -> dst%s sid = %u\n",
             (pSrcSurface->f.surfaceFlags & SVGA3D_SURFACE_SCREENTARGET) ? " st" : "", pSrcSurface->id,
             (pDstSurface->f.surfaceFlags & SVGA3D_SURFACE_SCREENTARGET) ? " st" : "", pDstSurface->id));

    /* Clip the box. */
    /** @todo Use [src|dst]SubResource to index p[Src|Dst]Surface->paMipmapLevels array directly. */
    uint32_t iSrcFace;
    uint32_t iSrcMipmap;
    vmsvga3dCalcMipmapAndFace(pSrcSurface->cLevels, srcSubResource, &iSrcMipmap, &iSrcFace);

    uint32_t iDstFace;
    uint32_t iDstMipmap;
    vmsvga3dCalcMipmapAndFace(pDstSurface->cLevels, dstSubResource, &iDstMipmap, &iDstFace);

    PVMSVGA3DMIPMAPLEVEL pSrcMipLevel;
    rc = vmsvga3dMipmapLevel(pSrcSurface, iSrcFace, iSrcMipmap, &pSrcMipLevel);
    ASSERT_GUEST_RETURN(RT_SUCCESS(rc), rc);

    PVMSVGA3DMIPMAPLEVEL pDstMipLevel;
    rc = vmsvga3dMipmapLevel(pDstSurface, iDstFace, iDstMipmap, &pDstMipLevel);
    ASSERT_GUEST_RETURN(RT_SUCCESS(rc), rc);

    SVGA3dBox clipBoxSrc = *pBoxSrc;
    vmsvgaR3ClipBox(&pSrcMipLevel->mipmapSize, &clipBoxSrc);

    SVGA3dBox clipBoxDst = *pBoxDst;
    vmsvgaR3ClipBox(&pDstMipLevel->mipmapSize, &clipBoxDst);

    ID3D11Resource *pDstResource = dxResource(pDstSurface);
    ID3D11Resource *pSrcResource = dxResource(pSrcSurface);

    D3D11_RENDER_TARGET_VIEW_DESC RTVDesc;
    RT_ZERO(RTVDesc);
    RTVDesc.Format = vmsvgaDXSurfaceFormat2Dxgi(pDstSurface->format);;
    RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    RTVDesc.Texture2D.MipSlice = dstSubResource;

    ID3D11RenderTargetView *pDstRenderTargetView;
    HRESULT hr = pDevice->pDevice->CreateRenderTargetView(pDstResource, &RTVDesc, &pDstRenderTargetView);
    AssertReturn(SUCCEEDED(hr), VERR_NOT_SUPPORTED);

    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc;
    RT_ZERO(SRVDesc);
    SRVDesc.Format = vmsvgaDXSurfaceFormat2Dxgi(pSrcSurface->format);
    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    SRVDesc.Texture2D.MostDetailedMip = srcSubResource;
    SRVDesc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView *pSrcShaderResourceView;
    hr = pDevice->pDevice->CreateShaderResourceView(pSrcResource, &SRVDesc, &pSrcShaderResourceView);
    AssertReturnStmt(SUCCEEDED(hr), D3D_RELEASE(pDstRenderTargetView), VERR_NOT_SUPPORTED);

    D3D11_RECT rectDst;
    rectDst.left   = pBoxDst->x;
    rectDst.top    = pBoxDst->y;
    rectDst.right  = pBoxDst->x + pBoxDst->w;
    rectDst.bottom = pBoxDst->y + pBoxDst->h;

    BlitFromTexture(&pDevice->Blitter, pDstRenderTargetView, (float)pDstMipLevel->mipmapSize.width, (float)pDstMipLevel->mipmapSize.height,
                    rectDst, pSrcShaderResourceView);

    D3D_RELEASE(pSrcShaderResourceView);
    D3D_RELEASE(pDstRenderTargetView);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXGenMips(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dShaderResourceViewId shaderResourceViewId)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pDevice, VERR_INVALID_STATE);

    DXVIEW *pDXView;
    int rc = dxEnsureShaderResourceView(pThisCC, pDXContext, shaderResourceViewId, &pDXView);
    AssertRCReturn(rc, rc);

    pDXDevice->pImmediateContext->GenerateMips(pDXView->u.pShaderResourceView);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXDefineShaderResourceView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dShaderResourceViewId shaderResourceViewId, SVGACOTableDXSRViewEntry const *pEntry)
{
    /* The view is created when it is used in setupPipeline. */
    RT_NOREF(pThisCC, pDXContext, shaderResourceViewId, pEntry);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXDestroyShaderResourceView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dShaderResourceViewId shaderResourceViewId)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXVIEW *pDXView = &pDXContext->pBackendDXContext->paShaderResourceView[shaderResourceViewId];
    return dxViewDestroy(pDXView);
}


static DECLCALLBACK(int) vmsvga3dBackDXDefineRenderTargetView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dRenderTargetViewId renderTargetViewId, SVGACOTableDXRTViewEntry const *pEntry)
{
    /* The view is created when it is used in setupPipeline or ClearView. */
    RT_NOREF(pThisCC, pDXContext, renderTargetViewId, pEntry);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXDestroyRenderTargetView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dRenderTargetViewId renderTargetViewId)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXVIEW *pDXView = &pDXContext->pBackendDXContext->paRenderTargetView[renderTargetViewId];
    return dxViewDestroy(pDXView);
}


static DECLCALLBACK(int) vmsvga3dBackDXDefineDepthStencilView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dDepthStencilViewId depthStencilViewId, SVGACOTableDXDSViewEntry const *pEntry)
{
    /* The view is created when it is used in setupPipeline or ClearView. */
    RT_NOREF(pThisCC, pDXContext, depthStencilViewId, pEntry);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXDestroyDepthStencilView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dDepthStencilViewId depthStencilViewId)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXVIEW *pDXView = &pDXContext->pBackendDXContext->paDepthStencilView[depthStencilViewId];
    return dxViewDestroy(pDXView);
}


static int dxDefineElementLayout(PVMSVGA3DDXCONTEXT pDXContext, SVGA3dElementLayoutId elementLayoutId, SVGACOTableDXElementLayoutEntry const *pEntry)
{
    DXELEMENTLAYOUT *pDXElementLayout = &pDXContext->pBackendDXContext->paElementLayout[elementLayoutId];
    D3D_RELEASE(pDXElementLayout->pElementLayout);
    pDXElementLayout->cElementDesc = 0;
    RT_ZERO(pDXElementLayout->aElementDesc);

    RT_NOREF(pEntry);

    return VINF_SUCCESS;
}


static int dxDestroyElementLayout(DXELEMENTLAYOUT *pDXElementLayout)
{
    D3D_RELEASE(pDXElementLayout->pElementLayout);
    pDXElementLayout->cElementDesc = 0;
    RT_ZERO(pDXElementLayout->aElementDesc);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXDefineElementLayout(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dElementLayoutId elementLayoutId, SVGACOTableDXElementLayoutEntry const *pEntry)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    RT_NOREF(pBackend);

    /* Not much can be done here because ID3D11Device::CreateInputLayout requires
     * a pShaderBytecodeWithInputSignature which is not known at this moment.
     * InputLayout object will be created in setupPipeline.
     */

    Assert(elementLayoutId == pEntry->elid);

    return dxDefineElementLayout(pDXContext, elementLayoutId, pEntry);
}


static DECLCALLBACK(int) vmsvga3dBackDXDestroyElementLayout(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dElementLayoutId elementLayoutId)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXELEMENTLAYOUT *pDXElementLayout = &pDXContext->pBackendDXContext->paElementLayout[elementLayoutId];
    dxDestroyElementLayout(pDXElementLayout);

    return VINF_SUCCESS;
}


static int dxDefineBlendState(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext,
                              SVGA3dBlendStateId blendId, SVGACOTableDXBlendStateEntry const *pEntry)
{
    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    HRESULT hr = dxBlendStateCreate(pDevice, pEntry, &pDXContext->pBackendDXContext->papBlendState[blendId]);
    if (SUCCEEDED(hr))
        return VINF_SUCCESS;
    return VERR_INVALID_STATE;
}


static DECLCALLBACK(int) vmsvga3dBackDXDefineBlendState(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext,
                                                        SVGA3dBlendStateId blendId, SVGACOTableDXBlendStateEntry const *pEntry)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    return dxDefineBlendState(pThisCC, pDXContext, blendId, pEntry);
}


static DECLCALLBACK(int) vmsvga3dBackDXDestroyBlendState(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dBlendStateId blendId)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    D3D_RELEASE(pDXContext->pBackendDXContext->papBlendState[blendId]);
    return VINF_SUCCESS;
}


static int dxDefineDepthStencilState(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dDepthStencilStateId depthStencilId, SVGACOTableDXDepthStencilEntry const *pEntry)
{
    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    HRESULT hr = dxDepthStencilStateCreate(pDevice, pEntry, &pDXContext->pBackendDXContext->papDepthStencilState[depthStencilId]);
    if (SUCCEEDED(hr))
        return VINF_SUCCESS;
    return VERR_INVALID_STATE;
}


static DECLCALLBACK(int) vmsvga3dBackDXDefineDepthStencilState(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dDepthStencilStateId depthStencilId, SVGACOTableDXDepthStencilEntry const *pEntry)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    return dxDefineDepthStencilState(pThisCC, pDXContext, depthStencilId, pEntry);
}


static DECLCALLBACK(int) vmsvga3dBackDXDestroyDepthStencilState(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dDepthStencilStateId depthStencilId)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    D3D_RELEASE(pDXContext->pBackendDXContext->papDepthStencilState[depthStencilId]);
    return VINF_SUCCESS;
}


static int dxDefineRasterizerState(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dRasterizerStateId rasterizerId, SVGACOTableDXRasterizerStateEntry const *pEntry)
{
    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    HRESULT hr = dxRasterizerStateCreate(pDevice, pEntry, &pDXContext->pBackendDXContext->papRasterizerState[rasterizerId]);
    if (SUCCEEDED(hr))
        return VINF_SUCCESS;
    return VERR_INVALID_STATE;
}


static DECLCALLBACK(int) vmsvga3dBackDXDefineRasterizerState(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dRasterizerStateId rasterizerId, SVGACOTableDXRasterizerStateEntry const *pEntry)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    return dxDefineRasterizerState(pThisCC, pDXContext, rasterizerId, pEntry);
}


static DECLCALLBACK(int) vmsvga3dBackDXDestroyRasterizerState(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dRasterizerStateId rasterizerId)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    D3D_RELEASE(pDXContext->pBackendDXContext->papRasterizerState[rasterizerId]);
    return VINF_SUCCESS;
}


static int dxDefineSamplerState(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dSamplerId samplerId, SVGACOTableDXSamplerEntry const *pEntry)
{
    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    HRESULT hr = dxSamplerStateCreate(pDevice, pEntry, &pDXContext->pBackendDXContext->papSamplerState[samplerId]);
    if (SUCCEEDED(hr))
        return VINF_SUCCESS;
    return VERR_INVALID_STATE;
}


static DECLCALLBACK(int) vmsvga3dBackDXDefineSamplerState(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dSamplerId samplerId, SVGACOTableDXSamplerEntry const *pEntry)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    return dxDefineSamplerState(pThisCC, pDXContext, samplerId, pEntry);
}


static DECLCALLBACK(int) vmsvga3dBackDXDestroySamplerState(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dSamplerId samplerId)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    D3D_RELEASE(pDXContext->pBackendDXContext->papSamplerState[samplerId]);
    return VINF_SUCCESS;
}


static int dxDefineShader(PVMSVGA3DDXCONTEXT pDXContext, SVGA3dShaderId shaderId, SVGACOTableDXShaderEntry const *pEntry)
{
    /** @todo A common approach for creation of COTable backend objects: runtime, empty DX COTable, live DX COTable. */
    DXSHADER *pDXShader = &pDXContext->pBackendDXContext->paShader[shaderId];
    Assert(pDXShader->enmShaderType == SVGA3D_SHADERTYPE_INVALID);

    /* Init the backend shader structure, if the shader has not been created yet. */
    pDXShader->enmShaderType = pEntry->type;
    pDXShader->pShader = NULL;
    pDXShader->soid = SVGA_ID_INVALID;

    return VINF_SUCCESS;
}


static int dxDestroyShader(DXSHADER *pDXShader)
{
    pDXShader->enmShaderType = SVGA3D_SHADERTYPE_INVALID;
    DXShaderFree(&pDXShader->shaderInfo);
    D3D_RELEASE(pDXShader->pShader);
    RTMemFree(pDXShader->pvDXBC);
    pDXShader->pvDXBC = NULL;
    pDXShader->cbDXBC = 0;
    pDXShader->soid = SVGA_ID_INVALID;
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXDefineShader(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dShaderId shaderId, SVGACOTableDXShaderEntry const *pEntry)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    return dxDefineShader(pDXContext, shaderId, pEntry);
}


static DECLCALLBACK(int) vmsvga3dBackDXDestroyShader(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dShaderId shaderId)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXSHADER *pDXShader = &pDXContext->pBackendDXContext->paShader[shaderId];
    dxDestroyShader(pDXShader);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXBindShader(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dShaderId shaderId, DXShaderInfo const *pShaderInfo)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    RT_NOREF(pBackend);

    DXSHADER *pDXShader = &pDXContext->pBackendDXContext->paShader[shaderId];
    if (pDXShader->pvDXBC)
    {
        /* New DXBC code and new shader must be created. */
        D3D_RELEASE(pDXShader->pShader);
        RTMemFree(pDXShader->pvDXBC);
        pDXShader->pvDXBC = NULL;
        pDXShader->cbDXBC = 0;
    }

    pDXShader->shaderInfo = *pShaderInfo;

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXDefineStreamOutput(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dStreamOutputId soid, SVGACOTableDXStreamOutputEntry const *pEntry)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXSTREAMOUTPUT *pDXStreamOutput = &pDXContext->pBackendDXContext->paStreamOutput[soid];
    dxDestroyStreamOutput(pDXStreamOutput);

    RT_NOREF(pEntry);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXDestroyStreamOutput(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dStreamOutputId soid)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXSTREAMOUTPUT *pDXStreamOutput = &pDXContext->pBackendDXContext->paStreamOutput[soid];
    dxDestroyStreamOutput(pDXStreamOutput);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXSetStreamOutput(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dStreamOutputId soid)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend, pDXContext, soid);

    return VINF_SUCCESS;
}


static int dxCOTableRealloc(void **ppvCOTable, uint32_t *pcCOTable, uint32_t cbEntry, uint32_t cEntries, uint32_t cValidEntries)
{
    uint32_t const cCOTableCurrent = *pcCOTable;

    if (*pcCOTable != cEntries)
    {
        /* Grow/shrink the array. */
        if (cEntries)
        {
            void *pvNew = RTMemRealloc(*ppvCOTable, cEntries * cbEntry);
            AssertReturn(pvNew, VERR_NO_MEMORY);
            *ppvCOTable = pvNew;
        }
        else
        {
            RTMemFree(*ppvCOTable);
            *ppvCOTable = NULL;
        }

        *pcCOTable = cEntries;
    }

    if (*ppvCOTable)
    {
        uint32_t const cEntriesToKeep = RT_MIN(cCOTableCurrent, cValidEntries);
        memset((uint8_t *)(*ppvCOTable) + cEntriesToKeep * cbEntry, 0, (cEntries - cEntriesToKeep) * cbEntry);
    }

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) vmsvga3dBackDXSetCOTable(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGACOTableType type, uint32_t cValidEntries)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    VMSVGA3DBACKENDDXCONTEXT *pBackendDXContext = pDXContext->pBackendDXContext;

    int rc = VINF_SUCCESS;

    /*
     * 1) Release current backend table, if exists;
     * 2) Reallocate memory for the new backend table;
     * 3) If cValidEntries is not zero, then re-define corresponding backend table elements.
     */
    switch (type)
    {
        case SVGA_COTABLE_RTVIEW:
            /* Clear current entries. */
            if (pBackendDXContext->paRenderTargetView)
            {
                for (uint32_t i = 0; i < pBackendDXContext->cRenderTargetView; ++i)
                {
                    DXVIEW *pDXView = &pBackendDXContext->paRenderTargetView[i];
                    if (i < cValidEntries)
                        dxViewRemoveFromList(pDXView); /* Remove from list because DXVIEW array will be reallocated. */
                    else
                        dxViewDestroy(pDXView);
                }
            }

            rc = dxCOTableRealloc((void **)&pBackendDXContext->paRenderTargetView, &pBackendDXContext->cRenderTargetView,
                                  sizeof(pBackendDXContext->paRenderTargetView[0]), pDXContext->cot.cRTView, cValidEntries);
            AssertRCBreak(rc);

            for (uint32_t i = 0; i < cValidEntries; ++i)
            {
                SVGACOTableDXRTViewEntry const *pEntry = &pDXContext->cot.paRTView[i];
                if (ASMMemFirstNonZero(pEntry, sizeof(*pEntry)) == NULL)
                    continue; /* Skip uninitialized entry. */

                /* Define views which were not defined yet in backend. */
                DXVIEW *pDXView = &pBackendDXContext->paRenderTargetView[i];
                /** @todo Verify that the pEntry content still corresponds to the view. */
                if (pDXView->u.pView)
                    dxViewAddToList(pThisCC, pDXView);
            }
            break;
        case SVGA_COTABLE_DSVIEW:
            if (pBackendDXContext->paDepthStencilView)
            {
                for (uint32_t i = 0; i < pBackendDXContext->cDepthStencilView; ++i)
                {
                    DXVIEW *pDXView = &pBackendDXContext->paDepthStencilView[i];
                    if (i < cValidEntries)
                        dxViewRemoveFromList(pDXView); /* Remove from list because DXVIEW array will be reallocated. */
                    else
                        dxViewDestroy(pDXView);
                }
            }

            rc = dxCOTableRealloc((void **)&pBackendDXContext->paDepthStencilView, &pBackendDXContext->cDepthStencilView,
                                  sizeof(pBackendDXContext->paDepthStencilView[0]), pDXContext->cot.cDSView, cValidEntries);
            AssertRCBreak(rc);

            for (uint32_t i = 0; i < cValidEntries; ++i)
            {
                SVGACOTableDXDSViewEntry const *pEntry = &pDXContext->cot.paDSView[i];
                if (ASMMemFirstNonZero(pEntry, sizeof(*pEntry)) == NULL)
                    continue; /* Skip uninitialized entry. */

                /* Define views which were not defined yet in backend. */
                DXVIEW *pDXView = &pBackendDXContext->paDepthStencilView[i];
                /** @todo Verify that the pEntry content still corresponds to the view. */
                if (pDXView->u.pView)
                    dxViewAddToList(pThisCC, pDXView);
            }
            break;
        case SVGA_COTABLE_SRVIEW:
            if (pBackendDXContext->paShaderResourceView)
            {
                for (uint32_t i = 0; i < pBackendDXContext->cShaderResourceView; ++i)
                {
                    /* Destroy the no longer used entries. */
                    DXVIEW *pDXView = &pBackendDXContext->paShaderResourceView[i];
                    if (i < cValidEntries)
                        dxViewRemoveFromList(pDXView); /* Remove from list because DXVIEW array will be reallocated. */
                    else
                        dxViewDestroy(pDXView);
                }
            }

            rc = dxCOTableRealloc((void **)&pBackendDXContext->paShaderResourceView, &pBackendDXContext->cShaderResourceView,
                                  sizeof(pBackendDXContext->paShaderResourceView[0]), pDXContext->cot.cSRView, cValidEntries);
            AssertRCBreak(rc);

            for (uint32_t i = 0; i < cValidEntries; ++i)
            {
                SVGACOTableDXSRViewEntry const *pEntry = &pDXContext->cot.paSRView[i];
                if (ASMMemFirstNonZero(pEntry, sizeof(*pEntry)) == NULL)
                    continue; /* Skip uninitialized entry. */

                DXVIEW *pDXView = &pBackendDXContext->paShaderResourceView[i];
                /** @todo Verify that the pEntry content still corresponds to the view. */
                if (pDXView->u.pView)
                    dxViewAddToList(pThisCC, pDXView);
            }
            break;
        case SVGA_COTABLE_ELEMENTLAYOUT:
            if (pBackendDXContext->paElementLayout)
            {
                for (uint32_t i = cValidEntries; i < pBackendDXContext->cElementLayout; ++i)
                    D3D_RELEASE(pBackendDXContext->paElementLayout[i].pElementLayout);
            }

            rc = dxCOTableRealloc((void **)&pBackendDXContext->paElementLayout, &pBackendDXContext->cElementLayout,
                                  sizeof(pBackendDXContext->paElementLayout[0]), pDXContext->cot.cElementLayout, cValidEntries);
            AssertRCBreak(rc);

            for (uint32_t i = 0; i < cValidEntries; ++i)
            {
                SVGACOTableDXElementLayoutEntry const *pEntry = &pDXContext->cot.paElementLayout[i];
                if (ASMMemFirstNonZero(pEntry, sizeof(*pEntry)) == NULL)
                    continue; /* Skip uninitialized entry. */

                dxDefineElementLayout(pDXContext, i, pEntry);
            }
            break;
        case SVGA_COTABLE_BLENDSTATE:
            if (pBackendDXContext->papBlendState)
            {
                for (uint32_t i = cValidEntries; i < pBackendDXContext->cBlendState; ++i)
                    D3D_RELEASE(pBackendDXContext->papBlendState[i]);
            }

            rc = dxCOTableRealloc((void **)&pBackendDXContext->papBlendState, &pBackendDXContext->cBlendState,
                                  sizeof(pBackendDXContext->papBlendState[0]), pDXContext->cot.cBlendState, cValidEntries);
            AssertRCBreak(rc);

            for (uint32_t i = 0; i < cValidEntries; ++i)
            {
                SVGACOTableDXBlendStateEntry const *pEntry = &pDXContext->cot.paBlendState[i];
                if (ASMMemFirstNonZero(pEntry, sizeof(*pEntry)) == NULL)
                    continue; /* Skip uninitialized entry. */

                dxDefineBlendState(pThisCC, pDXContext, i, pEntry);
            }
            break;
        case SVGA_COTABLE_DEPTHSTENCIL:
            if (pBackendDXContext->papDepthStencilState)
            {
                for (uint32_t i = cValidEntries; i < pBackendDXContext->cDepthStencilState; ++i)
                    D3D_RELEASE(pBackendDXContext->papDepthStencilState[i]);
            }

            rc = dxCOTableRealloc((void **)&pBackendDXContext->papDepthStencilState, &pBackendDXContext->cDepthStencilState,
                                  sizeof(pBackendDXContext->papDepthStencilState[0]), pDXContext->cot.cDepthStencil, cValidEntries);
            AssertRCBreak(rc);

            for (uint32_t i = 0; i < cValidEntries; ++i)
            {
                SVGACOTableDXDepthStencilEntry const *pEntry = &pDXContext->cot.paDepthStencil[i];
                if (ASMMemFirstNonZero(pEntry, sizeof(*pEntry)) == NULL)
                    continue; /* Skip uninitialized entry. */

                dxDefineDepthStencilState(pThisCC, pDXContext, i, pEntry);
            }
            break;
        case SVGA_COTABLE_RASTERIZERSTATE:
            if (pBackendDXContext->papRasterizerState)
            {
                for (uint32_t i = cValidEntries; i < pBackendDXContext->cRasterizerState; ++i)
                    D3D_RELEASE(pBackendDXContext->papRasterizerState[i]);
            }

            rc = dxCOTableRealloc((void **)&pBackendDXContext->papRasterizerState, &pBackendDXContext->cRasterizerState,
                                  sizeof(pBackendDXContext->papRasterizerState[0]), pDXContext->cot.cRasterizerState, cValidEntries);
            AssertRCBreak(rc);

            for (uint32_t i = 0; i < cValidEntries; ++i)
            {
                SVGACOTableDXRasterizerStateEntry const *pEntry = &pDXContext->cot.paRasterizerState[i];
                if (ASMMemFirstNonZero(pEntry, sizeof(*pEntry)) == NULL)
                    continue; /* Skip uninitialized entry. */

                dxDefineRasterizerState(pThisCC, pDXContext, i, pEntry);
            }
            break;
        case SVGA_COTABLE_SAMPLER:
            if (pBackendDXContext->papSamplerState)
            {
                for (uint32_t i = cValidEntries; i < pBackendDXContext->cSamplerState; ++i)
                    D3D_RELEASE(pBackendDXContext->papSamplerState[i]);
            }

            rc = dxCOTableRealloc((void **)&pBackendDXContext->papSamplerState, &pBackendDXContext->cSamplerState,
                                  sizeof(pBackendDXContext->papSamplerState[0]), pDXContext->cot.cSampler, cValidEntries);
            AssertRCBreak(rc);

            for (uint32_t i = 0; i < cValidEntries; ++i)
            {
                SVGACOTableDXSamplerEntry const *pEntry = &pDXContext->cot.paSampler[i];
                if (ASMMemFirstNonZero(pEntry, sizeof(*pEntry)) == NULL)
                    continue; /* Skip uninitialized entry. */

                dxDefineSamplerState(pThisCC, pDXContext, i, pEntry);
            }
            break;
        case SVGA_COTABLE_STREAMOUTPUT:
            if (pBackendDXContext->paStreamOutput)
            {
                for (uint32_t i = cValidEntries; i < pBackendDXContext->cStreamOutput; ++i)
                    dxDestroyStreamOutput(&pBackendDXContext->paStreamOutput[i]);
            }

            rc = dxCOTableRealloc((void **)&pBackendDXContext->paStreamOutput, &pBackendDXContext->cStreamOutput,
                                  sizeof(pBackendDXContext->paStreamOutput[0]), pDXContext->cot.cStreamOutput, cValidEntries);
            AssertRCBreak(rc);

            for (uint32_t i = 0; i < cValidEntries; ++i)
            {
                SVGACOTableDXStreamOutputEntry const *pEntry = &pDXContext->cot.paStreamOutput[i];
                /** @todo The caller must verify the COTable content using same rules as when a new entry is defined. */
                if (ASMMemFirstNonZero(pEntry, sizeof(*pEntry)) == NULL)
                    continue; /* Skip uninitialized entry. */

                /* Reset the stream output backend data. It will be re-created when a GS shader with this streamoutput
                 * will be set in setupPipeline.
                 */
                DXSTREAMOUTPUT *pDXStreamOutput = &pDXContext->pBackendDXContext->paStreamOutput[i];
                dxDestroyStreamOutput(pDXStreamOutput);
            }
            break;
        case SVGA_COTABLE_DXQUERY:
            if (pBackendDXContext->paQuery)
            {
                /* Destroy the no longer used entries. */
                for (uint32_t i = cValidEntries; i < pBackendDXContext->cQuery; ++i)
                    dxDestroyQuery(&pBackendDXContext->paQuery[i]);
            }

            rc = dxCOTableRealloc((void **)&pBackendDXContext->paQuery, &pBackendDXContext->cQuery,
                                  sizeof(pBackendDXContext->paQuery[0]), pDXContext->cot.cQuery, cValidEntries);
            AssertRCBreak(rc);

            for (uint32_t i = 0; i < cValidEntries; ++i)
            {
                SVGACOTableDXQueryEntry const *pEntry = &pDXContext->cot.paQuery[i];
                if (ASMMemFirstNonZero(pEntry, sizeof(*pEntry)) == NULL)
                    continue; /* Skip uninitialized entry. */

                /* Define queries which were not defined yet in backend. */
                DXQUERY *pDXQuery = &pBackendDXContext->paQuery[i];
                if (   pEntry->type != SVGA3D_QUERYTYPE_INVALID
                    && pDXQuery->pQuery == NULL)
                    dxDefineQuery(pThisCC, pDXContext, i, pEntry);
                else
                    Assert(pEntry->type == SVGA3D_QUERYTYPE_INVALID || pDXQuery->pQuery);
            }
            break;
        case SVGA_COTABLE_DXSHADER:
            if (pBackendDXContext->paShader)
            {
                /* Destroy the no longer used entries. */
                for (uint32_t i = cValidEntries; i < pBackendDXContext->cShader; ++i)
                    dxDestroyShader(&pBackendDXContext->paShader[i]);
            }

            rc = dxCOTableRealloc((void **)&pBackendDXContext->paShader, &pBackendDXContext->cShader,
                                  sizeof(pBackendDXContext->paShader[0]), pDXContext->cot.cShader, cValidEntries);
            AssertRCBreak(rc);

            for (uint32_t i = 0; i < cValidEntries; ++i)
            {
                SVGACOTableDXShaderEntry const *pEntry = &pDXContext->cot.paShader[i];
                /** @todo The caller must verify the COTable content using same rules as when a new entry is defined. */
                if (ASMMemFirstNonZero(pEntry, sizeof(*pEntry)) == NULL)
                    continue; /* Skip uninitialized entry. */

                /* Define shaders which were not defined yet in backend. */
                DXSHADER *pDXShader = &pBackendDXContext->paShader[i];
                if (   pEntry->type != SVGA3D_SHADERTYPE_INVALID
                    && pDXShader->enmShaderType == SVGA3D_SHADERTYPE_INVALID)
                    dxDefineShader(pDXContext, i, pEntry);
                else
                    Assert(pEntry->type == pDXShader->enmShaderType);

            }
            break;
        case SVGA_COTABLE_UAVIEW:
            if (pBackendDXContext->paUnorderedAccessView)
            {
                for (uint32_t i = 0; i < pBackendDXContext->cUnorderedAccessView; ++i)
                {
                    DXVIEW *pDXView = &pBackendDXContext->paUnorderedAccessView[i];
                    if (i < cValidEntries)
                        dxViewRemoveFromList(pDXView); /* Remove from list because DXVIEW array will be reallocated. */
                    else
                        dxViewDestroy(pDXView);
                }
            }

            rc = dxCOTableRealloc((void **)&pBackendDXContext->paUnorderedAccessView, &pBackendDXContext->cUnorderedAccessView,
                                  sizeof(pBackendDXContext->paUnorderedAccessView[0]), pDXContext->cot.cUAView, cValidEntries);
            AssertRCBreak(rc);

            for (uint32_t i = 0; i < cValidEntries; ++i)
            {
                SVGACOTableDXUAViewEntry const *pEntry = &pDXContext->cot.paUAView[i];
                if (ASMMemFirstNonZero(pEntry, sizeof(*pEntry)) == NULL)
                    continue; /* Skip uninitialized entry. */

                /* Define views which were not defined yet in backend. */
                DXVIEW *pDXView = &pBackendDXContext->paUnorderedAccessView[i];
                /** @todo Verify that the pEntry content still corresponds to the view. */
                if (pDXView->u.pView)
                    dxViewAddToList(pThisCC, pDXView);
            }
            break;
        case SVGA_COTABLE_MAX: break; /* Compiler warning */
        case VBSVGA_COTABLE_VIDEOPROCESSOR:
            if (pBackendDXContext->paVideoProcessor)
            {
                /* Destroy the no longer used entries. */
                for (uint32_t i = cValidEntries; i < pBackendDXContext->cVideoProcessor; ++i)
                    dxDestroyVideoProcessor(&pBackendDXContext->paVideoProcessor[i]);
            }

            rc = dxCOTableRealloc((void **)&pBackendDXContext->paVideoProcessor, &pBackendDXContext->cVideoProcessor,
                                  sizeof(pBackendDXContext->paVideoProcessor[0]), pDXContext->cot.cVideoProcessor, cValidEntries);
            AssertRCBreak(rc);

            for (uint32_t i = 0; i < cValidEntries; ++i)
            {
                VBSVGACOTableDXVideoProcessorEntry const *pEntry = &pDXContext->cot.paVideoProcessor[i];
                if (ASMMemFirstNonZero(pEntry, sizeof(*pEntry)) == NULL)
                    continue; /* Skip uninitialized entry. */

                DXVIDEOPROCESSOR *pDXVideoProcessor = &pBackendDXContext->paVideoProcessor[i];
                if (pDXVideoProcessor->pVideoProcessor == NULL)
                    dxCreateVideoProcessor(pThisCC, pDXContext, i, pEntry);
            }
            break;
        case VBSVGA_COTABLE_VDOV:
            if (pBackendDXContext->paVideoDecoderOutputView)
            {
                /* Destroy the no longer used entries. */
                for (uint32_t i = 0; i < pBackendDXContext->cVideoDecoderOutputView; ++i)
                {
                    DXVIEW *pDXView = &pBackendDXContext->paVideoDecoderOutputView[i];
                    if (i < cValidEntries)
                        dxViewRemoveFromList(pDXView); /* Remove from list because DXVIEW array will be reallocated. */
                    else
                        dxViewDestroy(pDXView);
                }
            }

            rc = dxCOTableRealloc((void **)&pBackendDXContext->paVideoDecoderOutputView, &pBackendDXContext->cVideoDecoderOutputView,
                                  sizeof(pBackendDXContext->paVideoDecoderOutputView[0]), pDXContext->cot.cVideoDecoderOutputView, cValidEntries);
            AssertRCBreak(rc);

            for (uint32_t i = 0; i < cValidEntries; ++i)
            {
                VBSVGACOTableDXVideoDecoderOutputViewEntry const *pEntry = &pDXContext->cot.paVideoDecoderOutputView[i];
                if (ASMMemFirstNonZero(pEntry, sizeof(*pEntry)) == NULL)
                    continue; /* Skip uninitialized entry. */

                DXVIEW *pDXView = &pBackendDXContext->paVideoDecoderOutputView[i];
                if (pDXView->u.pView)
                    dxViewAddToList(pThisCC, pDXView);
            }
            break;
        case VBSVGA_COTABLE_VIDEODECODER:
            if (pBackendDXContext->paVideoDecoder)
            {
                /* Destroy the no longer used entries. */
                for (uint32_t i = cValidEntries; i < pBackendDXContext->cVideoDecoder; ++i)
                    dxDestroyVideoDecoder(&pBackendDXContext->paVideoDecoder[i]);
            }

            rc = dxCOTableRealloc((void **)&pBackendDXContext->paVideoDecoder, &pBackendDXContext->cVideoDecoder,
                                  sizeof(pBackendDXContext->paVideoDecoder[0]), pDXContext->cot.cVideoDecoder, cValidEntries);
            AssertRCBreak(rc);

            for (uint32_t i = 0; i < cValidEntries; ++i)
            {
                VBSVGACOTableDXVideoDecoderEntry const *pEntry = &pDXContext->cot.paVideoDecoder[i];
                if (ASMMemFirstNonZero(pEntry, sizeof(*pEntry)) == NULL)
                    continue; /* Skip uninitialized entry. */

                DXVIDEODECODER *pDXVideoDecoder = &pBackendDXContext->paVideoDecoder[i];
                if (pDXVideoDecoder->pVideoDecoder == NULL)
                    dxCreateVideoDecoder(pThisCC, pDXContext, i, pEntry);
            }
            break;
        case VBSVGA_COTABLE_VPIV:
            if (pBackendDXContext->paVideoProcessorInputView)
            {
                /* Destroy the no longer used entries. */
                for (uint32_t i = 0; i < pBackendDXContext->cVideoProcessorInputView; ++i)
                {
                    DXVIEW *pDXView = &pBackendDXContext->paVideoProcessorInputView[i];
                    if (i < cValidEntries)
                        dxViewRemoveFromList(pDXView); /* Remove from list because DXVIEW array will be reallocated. */
                    else
                        dxViewDestroy(pDXView);
                }
            }

            rc = dxCOTableRealloc((void **)&pBackendDXContext->paVideoProcessorInputView, &pBackendDXContext->cVideoProcessorInputView,
                                  sizeof(pBackendDXContext->paVideoProcessorInputView[0]), pDXContext->cot.cVideoProcessorInputView, cValidEntries);
            AssertRCBreak(rc);

            for (uint32_t i = 0; i < cValidEntries; ++i)
            {
                VBSVGACOTableDXVideoProcessorInputViewEntry const *pEntry = &pDXContext->cot.paVideoProcessorInputView[i];
                if (ASMMemFirstNonZero(pEntry, sizeof(*pEntry)) == NULL)
                    continue; /* Skip uninitialized entry. */

                DXVIEW *pDXView = &pBackendDXContext->paVideoProcessorInputView[i];
                if (pDXView->u.pView)
                    dxViewAddToList(pThisCC, pDXView);
            }
            break;
        case VBSVGA_COTABLE_VPOV:
            if (pBackendDXContext->paVideoProcessorOutputView)
            {
                /* Destroy the no longer used entries. */
                for (uint32_t i = 0; i < pBackendDXContext->cVideoProcessorOutputView; ++i)
                {
                    DXVIEW *pDXView = &pBackendDXContext->paVideoProcessorOutputView[i];
                    if (i < cValidEntries)
                        dxViewRemoveFromList(pDXView); /* Remove from list because DXVIEW array will be reallocated. */
                    else
                        dxViewDestroy(pDXView);
                }
            }

            rc = dxCOTableRealloc((void **)&pBackendDXContext->paVideoProcessorOutputView, &pBackendDXContext->cVideoProcessorOutputView,
                                  sizeof(pBackendDXContext->paVideoProcessorOutputView[0]), pDXContext->cot.cVideoProcessorOutputView, cValidEntries);
            AssertRCBreak(rc);

            for (uint32_t i = 0; i < cValidEntries; ++i)
            {
                VBSVGACOTableDXVideoProcessorOutputViewEntry const *pEntry = &pDXContext->cot.paVideoProcessorOutputView[i];
                if (ASMMemFirstNonZero(pEntry, sizeof(*pEntry)) == NULL)
                    continue; /* Skip uninitialized entry. */

                DXVIEW *pDXView = &pBackendDXContext->paVideoProcessorOutputView[i];
                if (pDXView->u.pView)
                    dxViewAddToList(pThisCC, pDXView);
            }
            break;
        case VBSVGA_COTABLE_MAX: break; /* Compiler warning */
#ifndef DEBUG_sunlover
        default: break; /* Compiler warning. */
#endif
    }
    return rc;
}


static DECLCALLBACK(int) vmsvga3dBackDXBufferCopy(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackDXSurfaceCopyAndReadback(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackDXMoveQuery(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackDXBindAllShader(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackDXHint(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackDXBufferUpdate(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackDXCondBindAllShader(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackScreenCopy(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackIntraSurfaceCopy(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dSurfaceImageId const &surface, SVGA3dCopyBox const &box)
{
    RT_NOREF(pDXContext);

    LogFunc(("sid %u\n", surface.sid));

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    PVMSVGA3DBACKEND pBackend = pState->pBackend;

    PVMSVGA3DSURFACE pSurface;
    int rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, surface.sid, &pSurface);
    AssertRCReturn(rc, rc);

    PVMSVGA3DMIPMAPLEVEL pMipLevel;
    rc = vmsvga3dMipmapLevel(pSurface, surface.face, surface.mipmap, &pMipLevel);
    ASSERT_GUEST_RETURN(RT_SUCCESS(rc), rc);

    /* Clip the box. */
    SVGA3dCopyBox clipBox = box;
    vmsvgaR3ClipCopyBox(&pMipLevel->mipmapSize, &pMipLevel->mipmapSize, &clipBox);

    LogFunc(("surface%s sid = %u\n",
             pSurface->pBackendSurface ? "" : " sysmem", pSurface->id));

    if (pSurface->pBackendSurface)
    {
        /* Surface -> Surface. */
        DXDEVICE *pDXDevice = &pBackend->dxDevice;

        UINT DstSubresource = vmsvga3dCalcSubresource(surface.mipmap, surface.face, pSurface->cLevels);
        UINT DstX = clipBox.x;
        UINT DstY = clipBox.y;
        UINT DstZ = clipBox.z;

        UINT SrcSubresource = DstSubresource;
        D3D11_BOX SrcBox;
        SrcBox.left   = clipBox.srcx;
        SrcBox.top    = clipBox.srcy;
        SrcBox.front  = clipBox.srcz;
        SrcBox.right  = clipBox.srcx + clipBox.w;
        SrcBox.bottom = clipBox.srcy + clipBox.h;
        SrcBox.back   = clipBox.srcz + clipBox.d;

        ID3D11Resource *pDstResource;
        ID3D11Resource *pSrcResource;
        pDstResource = dxResource(pSurface);
        pSrcResource = pDstResource;

        pDXDevice->pImmediateContext->CopySubresourceRegion1(pDstResource, DstSubresource, DstX, DstY, DstZ,
                                                             pSrcResource, SrcSubresource, &SrcBox, 0);
    }
    else
    {
        /* Memory -> Memory. */
        uint32_t const cxBlocks = (clipBox.w + pSurface->cxBlock - 1) / pSurface->cxBlock;
        uint32_t const cyBlocks = (clipBox.h + pSurface->cyBlock - 1) / pSurface->cyBlock;
        uint32_t const cbRow = cxBlocks * pSurface->cbBlock;

        uint8_t const *pu8Src = (uint8_t *)pMipLevel->pSurfaceData
                        + (clipBox.srcx / pSurface->cxBlock) * pSurface->cbBlock
                        + (clipBox.srcy / pSurface->cyBlock) * pMipLevel->cbSurfacePitch
                        + clipBox.srcz * pMipLevel->cbSurfacePlane;

        uint8_t *pu8Dst = (uint8_t *)pMipLevel->pSurfaceData
                        + (clipBox.x / pSurface->cxBlock) * pSurface->cbBlock
                        + (clipBox.y / pSurface->cyBlock) * pMipLevel->cbSurfacePitch
                        + clipBox.z * pMipLevel->cbSurfacePlane;

        for (uint32_t z = 0; z < clipBox.d; ++z)
        {
            uint8_t const *pu8PlaneSrc = pu8Src;
            uint8_t *pu8PlaneDst = pu8Dst;

            for (uint32_t y = 0; y < cyBlocks; ++y)
            {
                memmove(pu8PlaneDst, pu8PlaneSrc, cbRow);
                pu8PlaneDst += pMipLevel->cbSurfacePitch;
                pu8PlaneSrc += pMipLevel->cbSurfacePitch;
            }

            pu8Src += pMipLevel->cbSurfacePlane;
            pu8Dst += pMipLevel->cbSurfacePlane;
        }
    }

    return rc;
}


static DECLCALLBACK(int) vmsvga3dBackDXResolveCopy(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext,
                                                   SVGA3dSurfaceId dstSid, uint32_t dstSubResource,
                                                   SVGA3dSurfaceId srcSid, uint32_t srcSubResource, SVGA3dSurfaceFormat copyFormat)
{
    RT_NOREF(pDXContext);

    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pDevice, VERR_INVALID_STATE);

    PVMSVGA3DSURFACE pSrcSurface;
    ID3D11Resource *pSrcResource;
    int rc = dxEnsureResource(pThisCC, srcSid, &pSrcSurface, &pSrcResource);
    AssertRCReturn(rc, rc);

    PVMSVGA3DSURFACE pDstSurface;
    ID3D11Resource *pDstResource;
    rc = dxEnsureResource(pThisCC, dstSid, &pDstSurface, &pDstResource);
    AssertRCReturn(rc, rc);

    LogFunc(("cid %d: src sid = %u -> dst sid = %u\n", pDXContext->cid, srcSid, dstSid));

    DXGI_FORMAT const dxgiFormat = vmsvgaDXSurfaceFormat2Dxgi(copyFormat);
    pDXDevice->pImmediateContext->ResolveSubresource(pDstResource, dstSubResource, pSrcResource, srcSubResource, dxgiFormat);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXPredResolveCopy(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackDXPredConvertRegion(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackDXPredConvert(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackWholeSurfaceCopy(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackDXDefineUAView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dUAViewId uaViewId, SVGACOTableDXUAViewEntry const *pEntry)
{
    /* The view is created in setupPipeline or ClearView. */
    RT_NOREF(pThisCC, pDXContext, uaViewId, pEntry);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXDestroyUAView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dUAViewId uaViewId)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXVIEW *pDXView = &pDXContext->pBackendDXContext->paUnorderedAccessView[uaViewId];
    return dxViewDestroy(pDXView);
}


static DECLCALLBACK(int) vmsvga3dBackDXClearUAViewUint(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dUAViewId uaViewId, uint32_t const aValues[4])
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pDevice, VERR_INVALID_STATE);

    DXVIEW *pDXView;
    int rc = dxEnsureUnorderedAccessView(pThisCC, pDXContext, uaViewId, &pDXView);
    AssertRCReturn(rc, rc);

    pDXDevice->pImmediateContext->ClearUnorderedAccessViewUint(pDXView->u.pUnorderedAccessView, aValues);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXClearUAViewFloat(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dUAViewId uaViewId, float const aValues[4])
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pDevice, VERR_INVALID_STATE);

    DXVIEW *pDXView;
    int rc = dxEnsureUnorderedAccessView(pThisCC, pDXContext, uaViewId, &pDXView);
    AssertRCReturn(rc, rc);

    pDXDevice->pImmediateContext->ClearUnorderedAccessViewFloat(pDXView->u.pUnorderedAccessView, aValues);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXCopyStructureCount(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dUAViewId srcUAViewId, SVGA3dSurfaceId destSid, uint32_t destByteOffset)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pDevice, VERR_INVALID_STATE);

    /* Get corresponding resource. Create the buffer if does not yet exist. */
    ID3D11Buffer *pDstBuffer;
    if (destSid != SVGA3D_INVALID_ID)
    {
        PVMSVGA3DSURFACE pSurface;
        ID3D11Resource *pResource;
        int rc = dxEnsureResource(pThisCC, destSid, &pSurface, &pResource);
        AssertRCReturn(rc, rc);
        AssertReturn(pSurface->pBackendSurface->enmResType == VMSVGA3D_RESTYPE_BUFFER, VERR_INVALID_STATE);

        pDstBuffer = (ID3D11Buffer *)pResource;
    }
    else
        pDstBuffer = NULL;

    ID3D11UnorderedAccessView *pSrcView;
    if (srcUAViewId != SVGA3D_INVALID_ID)
    {
        DXVIEW *pDXView = &pDXContext->pBackendDXContext->paUnorderedAccessView[srcUAViewId];
        AssertReturn(pDXView->u.pUnorderedAccessView, VERR_INVALID_STATE);
        pSrcView = pDXView->u.pUnorderedAccessView;
    }
    else
        pSrcView = NULL;

    pDXDevice->pImmediateContext->CopyStructureCount(pDstBuffer, destByteOffset, pSrcView);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXSetUAViews(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, uint32_t uavSpliceIndex, uint32_t cUAViewId, SVGA3dUAViewId const *paUAViewId)
{
    /* Views are set in setupPipeline. */
    RT_NOREF(pThisCC, pDXContext, uavSpliceIndex, cUAViewId, paUAViewId);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXDrawIndexedInstancedIndirect(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dSurfaceId argsBufferSid, uint32_t byteOffsetForArgs)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    /* Get corresponding resource. Create the buffer if does not yet exist. */
    ID3D11Buffer *pBufferForArgs;
    if (argsBufferSid != SVGA_ID_INVALID)
    {
        PVMSVGA3DSURFACE pSurface;
        ID3D11Resource *pResource;
        int rc = dxEnsureResource(pThisCC, argsBufferSid, &pSurface, &pResource);
        AssertRCReturn(rc, rc);
        AssertReturn(pSurface->pBackendSurface->enmResType == VMSVGA3D_RESTYPE_BUFFER, VERR_INVALID_STATE);

        pBufferForArgs = (ID3D11Buffer *)pResource;
    }
    else
        pBufferForArgs = NULL;

    dxSetupPipeline(pThisCC, pDXContext);

    Assert(pDXContext->svgaDXContext.inputAssembly.topology != SVGA3D_PRIMITIVE_TRIANGLEFAN);

    pDevice->pImmediateContext->DrawIndexedInstancedIndirect(pBufferForArgs, byteOffsetForArgs);

#ifdef DX_FLUSH_AFTER_DRAW
    dxDeviceFlush(pDevice);
#endif

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXDrawInstancedIndirect(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dSurfaceId argsBufferSid, uint32_t byteOffsetForArgs)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    /* Get corresponding resource. Create the buffer if does not yet exist. */
    ID3D11Buffer *pBufferForArgs;
    if (argsBufferSid != SVGA_ID_INVALID)
    {
        PVMSVGA3DSURFACE pSurface;
        ID3D11Resource *pResource;
        int rc = dxEnsureResource(pThisCC, argsBufferSid, &pSurface, &pResource);
        AssertRCReturn(rc, rc);
        AssertReturn(pSurface->pBackendSurface->enmResType == VMSVGA3D_RESTYPE_BUFFER, VERR_INVALID_STATE);

        pBufferForArgs = (ID3D11Buffer *)pResource;
    }
    else
        pBufferForArgs = NULL;

    dxSetupPipeline(pThisCC, pDXContext);

    Assert(pDXContext->svgaDXContext.inputAssembly.topology != SVGA3D_PRIMITIVE_TRIANGLEFAN);

    pDevice->pImmediateContext->DrawInstancedIndirect(pBufferForArgs, byteOffsetForArgs);

#ifdef DX_FLUSH_AFTER_DRAW
    dxDeviceFlush(pDevice);
#endif

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXDispatch(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    dxSetupPipeline(pThisCC, pDXContext);

    pDevice->pImmediateContext->Dispatch(threadGroupCountX, threadGroupCountY, threadGroupCountZ);

#ifdef DX_FLUSH_AFTER_DRAW
    dxDeviceFlush(pDevice);
#endif

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXDispatchIndirect(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackWriteZeroSurface(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackHintZeroSurface(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackDXTransferToBuffer(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackLogicOpsBitBlt(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackLogicOpsTransBlt(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackLogicOpsStretchBlt(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackLogicOpsColorFill(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackLogicOpsAlphaBlend(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackLogicOpsClearTypeBlend(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static int dxSetCSUnorderedAccessViews(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

//DEBUG_BREAKPOINT_TEST();
    uint32_t const *pUAIds = &pDXContext->svgaDXContext.csuaViewIds[0];
    ID3D11UnorderedAccessView *papUnorderedAccessView[SVGA3D_DX11_1_MAX_UAVIEWS];
    UINT aUAVInitialCounts[SVGA3D_DX11_1_MAX_UAVIEWS];
    uint32_t const cMaxUAViews = pDevice->FeatureLevel >= D3D_FEATURE_LEVEL_11_1
                               ? SVGA3D_DX11_1_MAX_UAVIEWS
                               : SVGA3D_MAX_UAVIEWS;
    for (uint32_t i = 0; i < cMaxUAViews; ++i)
    {
        papUnorderedAccessView[i] = NULL;
        aUAVInitialCounts[i] = (UINT)-1;

        SVGA3dUAViewId const uaViewId = pUAIds[i];
        if (uaViewId != SVGA3D_INVALID_ID)
        {
            ASSERT_GUEST_CONTINUE(uaViewId < pDXContext->cot.cUAView);

            DXVIEW *pDXView = &pDXContext->pBackendDXContext->paUnorderedAccessView[uaViewId];
            Assert(pDXView->u.pUnorderedAccessView);
            papUnorderedAccessView[i] = pDXView->u.pUnorderedAccessView;

            SVGACOTableDXUAViewEntry const *pEntry = &pDXContext->cot.paUAView[uaViewId];
            aUAVInitialCounts[i] = pEntry->structureCount;
        }
    }

    dxCSUnorderedAccessViewSet(pDevice, 0, cMaxUAViews, papUnorderedAccessView, aUAVInitialCounts);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXSetCSUAViews(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, uint32_t startIndex, uint32_t cUAViewId, SVGA3dUAViewId const *paUAViewId)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend, pDXContext);

    DXDEVICE *pDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDevice->pDevice, VERR_INVALID_STATE);

    RT_NOREF(startIndex, cUAViewId, paUAViewId);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXSetMinLOD(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackDXSetShaderIface(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackSurfaceStretchBltNonMSToMS(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(int) vmsvga3dBackDXBindShaderIface(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;

    RT_NOREF(pBackend, pDXContext);
    AssertFailed(); /** @todo Implement */
    return VERR_NOT_IMPLEMENTED;
}


/*
 *
 * Video decoding and processing callbacks.
 *
 */

/*
 * Conversion between the device and D3D11 constants.
 */

static D3D11_VIDEO_FRAME_FORMAT dxVideoFrameFormat(VBSVGA3dVideoFrameFormat FrameFormat)
{
    switch (FrameFormat)
    {
        case VBSVGA3D_VIDEO_FRAME_FORMAT_PROGRESSIVE:                   return D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        case VBSVGA3D_VIDEO_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST:    return D3D11_VIDEO_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST;
        case VBSVGA3D_VIDEO_FRAME_FORMAT_INTERLACED_BOTTOM_FIELD_FIRST: return D3D11_VIDEO_FRAME_FORMAT_INTERLACED_BOTTOM_FIELD_FIRST;
        default:
            ASSERT_GUEST_FAILED();
            break;
    }
    return D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
}


static D3D11_VIDEO_USAGE dxVideoUsage(VBSVGA3dVideoUsage Usage)
{
    switch (Usage)
    {
        case VBSVGA3D_VIDEO_USAGE_PLAYBACK_NORMAL: return D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
        case VBSVGA3D_VIDEO_USAGE_OPTIMAL_SPEED:   return D3D11_VIDEO_USAGE_OPTIMAL_SPEED;
        case VBSVGA3D_VIDEO_USAGE_OPTIMAL_QUALITY: return D3D11_VIDEO_USAGE_OPTIMAL_QUALITY;
        default:
            ASSERT_GUEST_FAILED();
            break;
    }
    return D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
}


static D3D11_VDOV_DIMENSION dxVDOVDimension(VBSVGA3dVDOVDimension ViewDimension)
{
    switch (ViewDimension)
    {
        case VBSVGA3D_VDOV_DIMENSION_UNKNOWN:   return D3D11_VDOV_DIMENSION_UNKNOWN;
        case VBSVGA3D_VDOV_DIMENSION_TEXTURE2D: return D3D11_VDOV_DIMENSION_TEXTURE2D;
        default:
            ASSERT_GUEST_FAILED();
            break;
    }
    return D3D11_VDOV_DIMENSION_UNKNOWN;
}


static D3D11_VPIV_DIMENSION dxVPIVDimension(VBSVGA3dVPIVDimension ViewDimension)
{
    switch (ViewDimension)
    {
        case VBSVGA3D_VPIV_DIMENSION_UNKNOWN:   return D3D11_VPIV_DIMENSION_UNKNOWN;
        case VBSVGA3D_VPIV_DIMENSION_TEXTURE2D: return D3D11_VPIV_DIMENSION_TEXTURE2D;
        default:
            ASSERT_GUEST_FAILED();
            break;
    }
    return D3D11_VPIV_DIMENSION_UNKNOWN;
}


static D3D11_VPOV_DIMENSION dxVPOVDimension(VBSVGA3dVPOVDimension ViewDimension)
{
    switch (ViewDimension)
    {
        case VBSVGA3D_VPOV_DIMENSION_UNKNOWN:        return D3D11_VPOV_DIMENSION_UNKNOWN;
        case VBSVGA3D_VPOV_DIMENSION_TEXTURE2D:      return D3D11_VPOV_DIMENSION_TEXTURE2D;
        case VBSVGA3D_VPOV_DIMENSION_TEXTURE2DARRAY: return D3D11_VPOV_DIMENSION_TEXTURE2DARRAY;
        default:
            ASSERT_GUEST_FAILED();
            break;
    }
    return D3D11_VPOV_DIMENSION_UNKNOWN;
}


static D3D11_VIDEO_DECODER_BUFFER_TYPE dxVideoDecoderBufferType(VBSVGA3dVideoDecoderBufferType BufferType)
{
    switch (BufferType)
    {
        case VBSVGA3D_VD_BUFFER_PICTURE_PARAMETERS:          return D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS;
        case VBSVGA3D_VD_BUFFER_MACROBLOCK_CONTROL:          return D3D11_VIDEO_DECODER_BUFFER_MACROBLOCK_CONTROL;
        case VBSVGA3D_VD_BUFFER_RESIDUAL_DIFFERENCE:         return D3D11_VIDEO_DECODER_BUFFER_RESIDUAL_DIFFERENCE;
        case VBSVGA3D_VD_BUFFER_DEBLOCKING_CONTROL:          return D3D11_VIDEO_DECODER_BUFFER_DEBLOCKING_CONTROL;
        case VBSVGA3D_VD_BUFFER_INVERSE_QUANTIZATION_MATRIX: return D3D11_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX;
        case VBSVGA3D_VD_BUFFER_SLICE_CONTROL:               return D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL;
        case VBSVGA3D_VD_BUFFER_BITSTREAM:                   return D3D11_VIDEO_DECODER_BUFFER_BITSTREAM;
        case VBSVGA3D_VD_BUFFER_MOTION_VECTOR:               return D3D11_VIDEO_DECODER_BUFFER_MOTION_VECTOR;
        case VBSVGA3D_VD_BUFFER_FILM_GRAIN:                  return D3D11_VIDEO_DECODER_BUFFER_FILM_GRAIN;
        default:
            ASSERT_GUEST_FAILED();
            break;
    }
    return D3D11_VIDEO_DECODER_BUFFER_BITSTREAM;
}


/*
 * D3D11 wrappers.
 */

static void dxVideoProcessorSetOutputTargetRect(DXDEVICE *pDXDevice, DXVIDEOPROCESSOR *pDXVideoProcessor, uint8 enable, SVGASignedRect const &outputRect)
{
    RECT OutputRect;
    OutputRect.left   = outputRect.left;
    OutputRect.top    = outputRect.top;
    OutputRect.right  = outputRect.right;
    OutputRect.bottom = outputRect.bottom;

    pDXDevice->pVideoContext->VideoProcessorSetOutputTargetRect(pDXVideoProcessor->pVideoProcessor, RT_BOOL(enable), &OutputRect);
}


static void dxVideoProcessorSetOutputBackgroundColor(DXDEVICE *pDXDevice, DXVIDEOPROCESSOR *pDXVideoProcessor, uint32 YCbCr, VBSVGA3dVideoColor const &color)
{
    D3D11_VIDEO_COLOR Color;
    Color.RGBA.R = color.r;
    Color.RGBA.G = color.g;
    Color.RGBA.B = color.b;
    Color.RGBA.A = color.a;

    pDXDevice->pVideoContext->VideoProcessorSetOutputBackgroundColor(pDXVideoProcessor->pVideoProcessor, RT_BOOL(YCbCr), &Color);
}


static void dxVideoProcessorSetOutputColorSpace(DXDEVICE *pDXDevice, DXVIDEOPROCESSOR *pDXVideoProcessor, VBSVGA3dVideoProcessorColorSpace const &colorSpace)
{
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE ColorSpace;
    ColorSpace.Usage         = colorSpace.Usage;
    ColorSpace.RGB_Range     = colorSpace.RGB_Range;
    ColorSpace.YCbCr_Matrix  = colorSpace.YCbCr_Matrix;
    ColorSpace.YCbCr_xvYCC   = colorSpace.YCbCr_xvYCC;
    ColorSpace.Nominal_Range = colorSpace.Nominal_Range;
    ColorSpace.Reserved      = colorSpace.Reserved;

    pDXDevice->pVideoContext->VideoProcessorSetOutputColorSpace(pDXVideoProcessor->pVideoProcessor, &ColorSpace);
}


static void dxVideoProcessorSetOutputAlphaFillMode(DXDEVICE *pDXDevice, DXVIDEOPROCESSOR *pDXVideoProcessor, VBSVGA3dVideoProcessorAlphaFillMode fillMode, uint32 streamIndex)
{
    D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE AlphaFillMode = (D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE)fillMode;

    pDXDevice->pVideoContext->VideoProcessorSetOutputAlphaFillMode(pDXVideoProcessor->pVideoProcessor, AlphaFillMode, streamIndex);
}


static void dxVideoProcessorSetOutputConstriction(DXDEVICE *pDXDevice, DXVIDEOPROCESSOR *pDXVideoProcessor, uint32 enable, uint32 width, uint32 height)
{
    SIZE Size;
    Size.cx = width;
    Size.cy = height;

    pDXDevice->pVideoContext->VideoProcessorSetOutputConstriction(pDXVideoProcessor->pVideoProcessor, RT_BOOL(enable), Size);
}


static void dxVideoProcessorSetOutputStereoMode(DXDEVICE *pDXDevice, DXVIDEOPROCESSOR *pDXVideoProcessor, uint32 enable)
{
    pDXDevice->pVideoContext->VideoProcessorSetOutputStereoMode(pDXVideoProcessor->pVideoProcessor, RT_BOOL(enable));
}


static void dxVideoProcessorSetStreamFrameFormat(DXDEVICE *pDXDevice, DXVIDEOPROCESSOR *pDXVideoProcessor, uint32 streamIndex, VBSVGA3dVideoFrameFormat format)
{
    D3D11_VIDEO_FRAME_FORMAT FrameFormat = (D3D11_VIDEO_FRAME_FORMAT)format;

    pDXDevice->pVideoContext->VideoProcessorSetStreamFrameFormat(pDXVideoProcessor->pVideoProcessor, streamIndex, FrameFormat);
}


static void dxVideoProcessorSetStreamColorSpace(DXDEVICE *pDXDevice, DXVIDEOPROCESSOR *pDXVideoProcessor, uint32 streamIndex, VBSVGA3dVideoProcessorColorSpace colorSpace)
{
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE ColorSpace;
    ColorSpace.Usage         = colorSpace.Usage;
    ColorSpace.RGB_Range     = colorSpace.RGB_Range;
    ColorSpace.YCbCr_Matrix  = colorSpace.YCbCr_Matrix;
    ColorSpace.YCbCr_xvYCC   = colorSpace.YCbCr_xvYCC;
    ColorSpace.Nominal_Range = colorSpace.Nominal_Range;
    ColorSpace.Reserved      = colorSpace.Reserved;

    pDXDevice->pVideoContext->VideoProcessorSetStreamColorSpace(pDXVideoProcessor->pVideoProcessor, streamIndex, &ColorSpace);
}


static void dxVideoProcessorSetStreamOutputRate(DXDEVICE *pDXDevice, DXVIDEOPROCESSOR *pDXVideoProcessor,
                                                uint32 streamIndex, VBSVGA3dVideoProcessorOutputRate outputRate, uint32 repeatFrame, SVGA3dFraction64 const &customRate)
{
    D3D11_VIDEO_PROCESSOR_OUTPUT_RATE OutputRate = (D3D11_VIDEO_PROCESSOR_OUTPUT_RATE)outputRate;
    DXGI_RATIONAL CustomRate;
    CustomRate.Numerator   = customRate.numerator;
    CustomRate.Denominator = customRate.denominator;

    pDXDevice->pVideoContext->VideoProcessorSetStreamOutputRate(pDXVideoProcessor->pVideoProcessor, streamIndex, OutputRate, RT_BOOL(repeatFrame), &CustomRate);
}


static void dxVideoProcessorSetStreamSourceRect(DXDEVICE *pDXDevice, DXVIDEOPROCESSOR *pDXVideoProcessor,
                                                uint32 streamIndex, uint32 enable, SVGASignedRect const &sourceRect)
{
    RECT Rect;
    Rect.left   = sourceRect.left;
    Rect.top    = sourceRect.top;
    Rect.right  = sourceRect.right;
    Rect.bottom = sourceRect.bottom;

    pDXDevice->pVideoContext->VideoProcessorSetStreamSourceRect(pDXVideoProcessor->pVideoProcessor, streamIndex, RT_BOOL(enable), &Rect);
}


static void dxVideoProcessorSetStreamDestRect(DXDEVICE *pDXDevice, DXVIDEOPROCESSOR *pDXVideoProcessor,
                                              uint32 streamIndex, uint32 enable, SVGASignedRect const &destRect)
{
    RECT Rect;
    Rect.left   = destRect.left;
    Rect.top    = destRect.top;
    Rect.right  = destRect.right;
    Rect.bottom = destRect.bottom;

    pDXDevice->pVideoContext->VideoProcessorSetStreamDestRect(pDXVideoProcessor->pVideoProcessor, streamIndex, RT_BOOL(enable), &Rect);
}


static void dxVideoProcessorSetStreamAlpha(DXDEVICE *pDXDevice, DXVIDEOPROCESSOR *pDXVideoProcessor,
                                           uint32 streamIndex, uint32 enable, float alpha)
{
    pDXDevice->pVideoContext->VideoProcessorSetStreamAlpha(pDXVideoProcessor->pVideoProcessor, streamIndex, RT_BOOL(enable), alpha);
}


static void dxVideoProcessorSetStreamPalette(DXDEVICE *pDXDevice, DXVIDEOPROCESSOR *pDXVideoProcessor,
                                             uint32 streamIndex, uint32_t cEntries, uint32_t const *paEntries)
{
    pDXDevice->pVideoContext->VideoProcessorSetStreamPalette(pDXVideoProcessor->pVideoProcessor, streamIndex, cEntries, paEntries);
}


static void dxVideoProcessorSetStreamPixelAspectRatio(DXDEVICE *pDXDevice, DXVIDEOPROCESSOR *pDXVideoProcessor,
                                                      uint32 streamIndex, uint32 enable, SVGA3dFraction64 const &sourceRatio, SVGA3dFraction64 const &destRatio)
{
    DXGI_RATIONAL SourceAspectRatio;
    SourceAspectRatio.Numerator   = sourceRatio.numerator;
    SourceAspectRatio.Denominator = sourceRatio.denominator;

    DXGI_RATIONAL DestinationAspectRatio;
    DestinationAspectRatio.Numerator   = destRatio.numerator;
    DestinationAspectRatio.Denominator = destRatio.denominator;

    pDXDevice->pVideoContext->VideoProcessorSetStreamPixelAspectRatio(pDXVideoProcessor->pVideoProcessor, streamIndex, RT_BOOL(enable), &SourceAspectRatio, &DestinationAspectRatio);
}


static void dxVideoProcessorSetStreamLumaKey(DXDEVICE *pDXDevice, DXVIDEOPROCESSOR *pDXVideoProcessor,
                                             uint32 streamIndex, uint32 enable, float lower, float upper)
{
    pDXDevice->pVideoContext->VideoProcessorSetStreamLumaKey(pDXVideoProcessor->pVideoProcessor, streamIndex, RT_BOOL(enable), lower, upper);
}


static void dxVideoProcessorSetStreamStereoFormat(DXDEVICE *pDXDevice, DXVIDEOPROCESSOR *pDXVideoProcessor,
                                                  uint32 streamIndex, uint32 enable, VBSVGA3dVideoProcessorStereoFormat stereoFormat,
                                                  uint8 leftViewFrame0, uint8 baseViewFrame0, VBSVGA3dVideoProcessorStereoFlipMode flipMode, int32 monoOffset)
{
    D3D11_VIDEO_PROCESSOR_STEREO_FORMAT Format = (D3D11_VIDEO_PROCESSOR_STEREO_FORMAT)stereoFormat;
    D3D11_VIDEO_PROCESSOR_STEREO_FLIP_MODE FlipMode = (D3D11_VIDEO_PROCESSOR_STEREO_FLIP_MODE)flipMode;

    pDXDevice->pVideoContext->VideoProcessorSetStreamStereoFormat(pDXVideoProcessor->pVideoProcessor, streamIndex, RT_BOOL(enable), Format, RT_BOOL(leftViewFrame0), RT_BOOL(baseViewFrame0), FlipMode, monoOffset);
}


static void dxVideoProcessorSetStreamAutoProcessingMode(DXDEVICE *pDXDevice, DXVIDEOPROCESSOR *pDXVideoProcessor,
                                                        uint32 streamIndex, uint32 enable)
{
    pDXDevice->pVideoContext->VideoProcessorSetStreamAutoProcessingMode(pDXVideoProcessor->pVideoProcessor, streamIndex, RT_BOOL(enable));
}


static void dxVideoProcessorSetStreamFilter(DXDEVICE *pDXDevice, DXVIDEOPROCESSOR *pDXVideoProcessor,
                                            uint32 streamIndex, uint32 enable, VBSVGA3dVideoProcessorFilter filter, int32 level)
{
    D3D11_VIDEO_PROCESSOR_FILTER Filter = (D3D11_VIDEO_PROCESSOR_FILTER)filter;

    pDXDevice->pVideoContext->VideoProcessorSetStreamFilter(pDXVideoProcessor->pVideoProcessor, streamIndex, Filter, RT_BOOL(enable), level);
}


static void dxVideoProcessorSetStreamRotation(DXDEVICE *pDXDevice, DXVIDEOPROCESSOR *pDXVideoProcessor,
                                              uint32 streamIndex, uint32 enable, VBSVGA3dVideoProcessorRotation rotation)
{
    D3D11_VIDEO_PROCESSOR_ROTATION Rotation = (D3D11_VIDEO_PROCESSOR_ROTATION)rotation;

    pDXDevice->pVideoContext->VideoProcessorSetStreamRotation(pDXVideoProcessor->pVideoProcessor, streamIndex, RT_BOOL(enable), Rotation);
}


static int dxCreateVideoDecoderOutputView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoDecoderOutputViewId videoDecoderOutputViewId, VBSVGACOTableDXVideoDecoderOutputViewEntry const *pEntry)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoDevice, VERR_INVALID_STATE);

    PVMSVGA3DSURFACE pSurface;
    ID3D11Resource *pResource;
    int rc = dxEnsureResource(pThisCC, pEntry->sid, &pSurface, &pResource);
    AssertRCReturn(rc, rc);

    DXVIEW *pView = &pDXContext->pBackendDXContext->paVideoDecoderOutputView[videoDecoderOutputViewId];
    Assert(pView->u.pView == NULL);

    D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC Desc;
    RT_ZERO(Desc);
    memcpy(&Desc.DecodeProfile, &pEntry->desc.DecodeProfile, sizeof(GUID));
    Desc.ViewDimension        = dxVDOVDimension(pEntry->desc.ViewDimension);
    Desc.Texture2D.ArraySlice = pEntry->desc.Texture2D.ArraySlice;

    ID3D11VideoDecoderOutputView *pVDOView;
    HRESULT hr = pDXDevice->pVideoDevice->CreateVideoDecoderOutputView(pResource, &Desc, &pVDOView);
    AssertReturn(SUCCEEDED(hr), VERR_NOT_SUPPORTED);

    return dxViewInit(pView, pSurface, pDXContext, videoDecoderOutputViewId, VMSVGA3D_VIEWTYPE_VIDEODECODEROUTPUT, pVDOView);
}


static int dxCreateVideoProcessorInputView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorInputViewId videoProcessorInputViewId, VBSVGACOTableDXVideoProcessorInputViewEntry const *pEntry)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoDevice, VERR_INVALID_STATE);

    PVMSVGA3DSURFACE pSurface;
    ID3D11Resource *pResource;
    int rc = dxEnsureResource(pThisCC, pEntry->sid, &pSurface, &pResource);
    AssertRCReturn(rc, rc);

    DXVIEW *pView = &pDXContext->pBackendDXContext->paVideoProcessorInputView[videoProcessorInputViewId];
    Assert(pView->u.pView == NULL);

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC ContentDesc;
    RT_ZERO(ContentDesc);
    ContentDesc.InputFrameFormat            = dxVideoFrameFormat(pEntry->contentDesc.InputFrameFormat);
    ContentDesc.InputFrameRate.Numerator    = pEntry->contentDesc.InputFrameRate.numerator;
    ContentDesc.InputFrameRate.Denominator  = pEntry->contentDesc.InputFrameRate.denominator;
    ContentDesc.InputWidth                  = pEntry->contentDesc.InputWidth;
    ContentDesc.InputHeight                 = pEntry->contentDesc.InputHeight;
    ContentDesc.OutputFrameRate.Numerator   = pEntry->contentDesc.OutputFrameRate.numerator;
    ContentDesc.OutputFrameRate.Denominator = pEntry->contentDesc.OutputFrameRate.denominator;
    ContentDesc.OutputWidth                 = pEntry->contentDesc.OutputWidth;
    ContentDesc.OutputHeight                = pEntry->contentDesc.OutputHeight;
    ContentDesc.Usage                       = dxVideoUsage(pEntry->contentDesc.Usage);

    ID3D11VideoProcessorEnumerator *pEnum;
    HRESULT hr = pDXDevice->pVideoDevice->CreateVideoProcessorEnumerator(&ContentDesc, &pEnum);
    AssertReturn(SUCCEEDED(hr), VERR_NOT_SUPPORTED);

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC Desc;
    RT_ZERO(Desc);
    Desc.FourCC               = pEntry->desc.FourCC;
    Desc.ViewDimension        = dxVPIVDimension(pEntry->desc.ViewDimension);
    Desc.Texture2D.MipSlice   = pEntry->desc.Texture2D.MipSlice;
    Desc.Texture2D.ArraySlice = pEntry->desc.Texture2D.ArraySlice;

    ID3D11VideoProcessorInputView *pVPIView;
    hr = pDXDevice->pVideoDevice->CreateVideoProcessorInputView(pResource, pEnum, &Desc, &pVPIView);
    D3D_RELEASE(pEnum);
    AssertReturn(SUCCEEDED(hr), VERR_NOT_SUPPORTED);

    return dxViewInit(pView, pSurface, pDXContext, videoProcessorInputViewId, VMSVGA3D_VIEWTYPE_VIDEOPROCESSORINPUT, pVPIView);
}


static int dxCreateVideoProcessorOutputView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorOutputViewId videoProcessorOutputViewId, VBSVGACOTableDXVideoProcessorOutputViewEntry const *pEntry)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoDevice, VERR_INVALID_STATE);

    PVMSVGA3DSURFACE pSurface;
    ID3D11Resource *pResource;
    int rc = dxEnsureResource(pThisCC, pEntry->sid, &pSurface, &pResource);
    AssertRCReturn(rc, rc);

    DXVIEW *pView = &pDXContext->pBackendDXContext->paVideoProcessorOutputView[videoProcessorOutputViewId];
    Assert(pView->u.pView == NULL);

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC ContentDesc;
    RT_ZERO(ContentDesc);
    ContentDesc.InputFrameFormat            = dxVideoFrameFormat(pEntry->contentDesc.InputFrameFormat);
    ContentDesc.InputFrameRate.Numerator    = pEntry->contentDesc.InputFrameRate.numerator;
    ContentDesc.InputFrameRate.Denominator  = pEntry->contentDesc.InputFrameRate.denominator;
    ContentDesc.InputWidth                  = pEntry->contentDesc.InputWidth;
    ContentDesc.InputHeight                 = pEntry->contentDesc.InputHeight;
    ContentDesc.OutputFrameRate.Numerator   = pEntry->contentDesc.OutputFrameRate.numerator;
    ContentDesc.OutputFrameRate.Denominator = pEntry->contentDesc.OutputFrameRate.denominator;
    ContentDesc.OutputWidth                 = pEntry->contentDesc.OutputWidth;
    ContentDesc.OutputHeight                = pEntry->contentDesc.OutputHeight;
    ContentDesc.Usage                       = dxVideoUsage(pEntry->contentDesc.Usage);

    ID3D11VideoProcessorEnumerator *pEnum;
    HRESULT hr = pDXDevice->pVideoDevice->CreateVideoProcessorEnumerator(&ContentDesc, &pEnum);
    AssertReturn(SUCCEEDED(hr), VERR_NOT_SUPPORTED);

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC Desc;
    RT_ZERO(Desc);
    Desc.ViewDimension                      = dxVPOVDimension(pEntry->desc.ViewDimension);
    if (Desc.ViewDimension == D3D11_VPOV_DIMENSION_TEXTURE2D)
    {
        Desc.Texture2D.MipSlice             = pEntry->desc.Texture2D.MipSlice;
    }
    else if (Desc.ViewDimension == D3D11_VPOV_DIMENSION_TEXTURE2DARRAY)
    {
        Desc.Texture2DArray.MipSlice        = pEntry->desc.Texture2DArray.MipSlice;
        Desc.Texture2DArray.FirstArraySlice = pEntry->desc.Texture2DArray.FirstArraySlice;
        Desc.Texture2DArray.ArraySize       = pEntry->desc.Texture2DArray.ArraySize;
    }

    ID3D11VideoProcessorOutputView *pVPOView;
    hr = pDXDevice->pVideoDevice->CreateVideoProcessorOutputView(pResource, pEnum, &Desc, &pVPOView);
    D3D_RELEASE(pEnum);
    AssertReturn(SUCCEEDED(hr), VERR_NOT_SUPPORTED);

    return dxViewInit(pView, pSurface, pDXContext, videoProcessorOutputViewId, VMSVGA3D_VIEWTYPE_VIDEOPROCESSOROUTPUT, pVPOView);
}


static int dxEnsureVideoDecoderOutputView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoDecoderOutputViewId viewId, DXVIEW **ppResult)
{
    ASSERT_GUEST_RETURN(viewId < pDXContext->cot.cVideoDecoderOutputView, VERR_INVALID_PARAMETER);

    DXVIEW *pDXView = &pDXContext->pBackendDXContext->paVideoDecoderOutputView[viewId];
    if (!pDXView->u.pView)
    {
        VBSVGACOTableDXVideoDecoderOutputViewEntry const *pEntry = &pDXContext->cot.paVideoDecoderOutputView[viewId];
        int rc = dxCreateVideoDecoderOutputView(pThisCC, pDXContext, viewId, pEntry);
        AssertRCReturn(rc, rc);
    }
    *ppResult = pDXView;
    return VINF_SUCCESS;
}


static int dxEnsureVideoProcessorInputView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorInputViewId viewId, DXVIEW **ppResult)
{
    ASSERT_GUEST_RETURN(viewId < pDXContext->cot.cVideoProcessorInputView, VERR_INVALID_PARAMETER);

    DXVIEW *pDXView = &pDXContext->pBackendDXContext->paVideoProcessorInputView[viewId];
    if (!pDXView->u.pView)
    {
        VBSVGACOTableDXVideoProcessorInputViewEntry const *pEntry = &pDXContext->cot.paVideoProcessorInputView[viewId];
        int rc = dxCreateVideoProcessorInputView(pThisCC, pDXContext, viewId, pEntry);
        AssertRCReturn(rc, rc);
    }
    *ppResult = pDXView;
    return VINF_SUCCESS;
}


static int dxEnsureVideoProcessorOutputView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorOutputViewId viewId, DXVIEW **ppResult)
{
    ASSERT_GUEST_RETURN(viewId < pDXContext->cot.cVideoProcessorOutputView, VERR_INVALID_PARAMETER);

    DXVIEW *pDXView = &pDXContext->pBackendDXContext->paVideoProcessorOutputView[viewId];
    if (!pDXView->u.pView)
    {
        VBSVGACOTableDXVideoProcessorOutputViewEntry const *pEntry = &pDXContext->cot.paVideoProcessorOutputView[viewId];
        int rc = dxCreateVideoProcessorOutputView(pThisCC, pDXContext, viewId, pEntry);
        AssertRCReturn(rc, rc);
    }
    *ppResult = pDXView;
    return VINF_SUCCESS;
}


static int dxVideoDecoderBeginFrame(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext,
                                    VBSVGA3dVideoDecoderId videoDecoderId,
                                    VBSVGA3dVideoDecoderOutputViewId videoDecoderOutputViewId)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    DXVIDEODECODER *pDXVideoDecoder = &pDXContext->pBackendDXContext->paVideoDecoder[videoDecoderId];

    DXVIEW *pDXView;
    int rc = dxEnsureVideoDecoderOutputView(pThisCC, pDXContext, videoDecoderOutputViewId, &pDXView);
    AssertRCReturn(rc, rc);

    HRESULT hr = pDXDevice->pVideoContext->DecoderBeginFrame(pDXVideoDecoder->pVideoDecoder,
                                                             pDXView->u.pVideoDecoderOutputView,
                                                             0, NULL);
    AssertReturn(SUCCEEDED(hr), VERR_NOT_SUPPORTED);

    return VINF_SUCCESS;
}


static void dxSetupVideoProcessor(DXDEVICE *pDXDevice, DXVIDEOPROCESSOR *pDXVideoProcessor, VBSVGACOTableDXVideoProcessorEntry const *pEntry)
{
    if (pEntry->output.SetMask & VBSVGA3D_VP_SET_OUTPUT_TARGET_RECT)
        dxVideoProcessorSetOutputTargetRect(pDXDevice, pDXVideoProcessor, pEntry->output.TargetRectEnable, pEntry->output.TargetRect);

    if (pEntry->output.SetMask & VBSVGA3D_VP_SET_OUTPUT_BACKGROUND_COLOR)
        dxVideoProcessorSetOutputBackgroundColor(pDXDevice, pDXVideoProcessor, pEntry->output.BackgroundColorYCbCr, pEntry->output.BackgroundColor);

    if (pEntry->output.SetMask & VBSVGA3D_VP_SET_OUTPUT_COLOR_SPACE)
        dxVideoProcessorSetOutputColorSpace(pDXDevice, pDXVideoProcessor, pEntry->output.ColorSpace);

    if (pEntry->output.SetMask & VBSVGA3D_VP_SET_OUTPUT_ALPHA_FILL_MODE)
        dxVideoProcessorSetOutputAlphaFillMode(pDXDevice, pDXVideoProcessor, pEntry->output.AlphaFillMode, pEntry->output.AlphaFillStreamIndex);

    if (pEntry->output.SetMask & VBSVGA3D_VP_SET_OUTPUT_CONSTRICTION)
        dxVideoProcessorSetOutputConstriction(pDXDevice, pDXVideoProcessor, pEntry->output.ConstrictionEnable, pEntry->output.ConstrictionWidth, pEntry->output.ConstrictionHeight);

    if (pEntry->output.SetMask & VBSVGA3D_VP_SET_OUTPUT_STEREO_MODE)
        dxVideoProcessorSetOutputStereoMode(pDXDevice, pDXVideoProcessor, pEntry->output.StereoModeEnable);

    for (uint32_t i = 0; i < RT_ELEMENTS(pEntry->aStreamState); ++i)
    {
        VBSVGA3dVideoProcessorStreamState const *pStreamState = &pEntry->aStreamState[i];
        if (pStreamState->SetMask & VBSVGA3D_VP_SET_STREAM_FRAME_FORMAT)
            dxVideoProcessorSetStreamFrameFormat(pDXDevice, pDXVideoProcessor, i, pStreamState->FrameFormat);

        if (pStreamState->SetMask & VBSVGA3D_VP_SET_STREAM_COLOR_SPACE)
            dxVideoProcessorSetStreamColorSpace(pDXDevice, pDXVideoProcessor, i, pStreamState->ColorSpace);

        if (pStreamState->SetMask & VBSVGA3D_VP_SET_STREAM_OUTPUT_RATE)
            dxVideoProcessorSetStreamOutputRate(pDXDevice, pDXVideoProcessor, i, pStreamState->OutputRate, pStreamState->RepeatFrame, pStreamState->CustomRate);

        if (pStreamState->SetMask & VBSVGA3D_VP_SET_STREAM_SOURCE_RECT)
            dxVideoProcessorSetStreamSourceRect(pDXDevice, pDXVideoProcessor, i, pStreamState->SourceRectEnable, pStreamState->SourceRect);

        if (pStreamState->SetMask & VBSVGA3D_VP_SET_STREAM_DEST_RECT)
            dxVideoProcessorSetStreamDestRect(pDXDevice, pDXVideoProcessor, i, pStreamState->DestRectEnable, pStreamState->DestRect);

        if (pStreamState->SetMask & VBSVGA3D_VP_SET_STREAM_ALPHA)
            dxVideoProcessorSetStreamAlpha(pDXDevice, pDXVideoProcessor, i, pStreamState->AlphaEnable, pStreamState->Alpha);

        if (pStreamState->SetMask & VBSVGA3D_VP_SET_STREAM_PALETTE)
            dxVideoProcessorSetStreamPalette(pDXDevice, pDXVideoProcessor, i, pStreamState->PaletteCount, &pStreamState->aPalette[0]);\

        if (pStreamState->SetMask & VBSVGA3D_VP_SET_STREAM_ASPECT_RATIO)
            dxVideoProcessorSetStreamPixelAspectRatio(pDXDevice, pDXVideoProcessor, i, pStreamState->AspectRatioEnable, pStreamState->AspectSourceRatio, pStreamState->AspectDestRatio);

        if (pStreamState->SetMask & VBSVGA3D_VP_SET_STREAM_LUMA_KEY)
            dxVideoProcessorSetStreamLumaKey(pDXDevice, pDXVideoProcessor, i, pStreamState->LumaKeyEnable, pStreamState->LumaKeyLower, pStreamState->LumaKeyUpper);

        if (pStreamState->SetMask & VBSVGA3D_VP_SET_STREAM_STEREO_FORMAT)
            dxVideoProcessorSetStreamStereoFormat(pDXDevice, pDXVideoProcessor, i, pStreamState->StereoFormatEnable, pStreamState->StereoFormat,
                                                  pStreamState->LeftViewFrame0, pStreamState->BaseViewFrame0, pStreamState->FlipMode, pStreamState->MonoOffset);

        if (pStreamState->SetMask & VBSVGA3D_VP_SET_STREAM_AUTO_PROCESSING_MODE)
            dxVideoProcessorSetStreamAutoProcessingMode(pDXDevice, pDXVideoProcessor, i, pStreamState->AutoProcessingModeEnable);

        if (pStreamState->SetMask & VBSVGA3D_VP_SET_STREAM_FILTER)
        {
            for (uint32_t idxFilter = 0; idxFilter < VBSVGA3D_VP_MAX_FILTER_COUNT; ++idxFilter)
            {
                uint32_t const enable = pStreamState->FilterEnableMask & ~(1 << idxFilter);
                int32 const level = pStreamState->aFilter[idxFilter].Level;
                dxVideoProcessorSetStreamFilter(pDXDevice, pDXVideoProcessor, i, enable, (VBSVGA3dVideoProcessorFilter)idxFilter, level);
            }
        }

        if (pStreamState->SetMask & VBSVGA3D_VP_SET_STREAM_ROTATION)
            dxVideoProcessorSetStreamRotation(pDXDevice, pDXVideoProcessor, i, pStreamState->RotationEnable, pStreamState->Rotation);
    }
}


static int dxCreateVideoProcessor(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId, VBSVGACOTableDXVideoProcessorEntry const *pEntry)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoDevice, VERR_INVALID_STATE);

    DXVIDEOPROCESSOR *pDXVideoProcessor = &pDXContext->pBackendDXContext->paVideoProcessor[videoProcessorId];

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC Desc;
    RT_ZERO(Desc);
    Desc.InputFrameFormat            = dxVideoFrameFormat(pEntry->desc.InputFrameFormat);
    Desc.InputFrameRate.Numerator    = pEntry->desc.InputFrameRate.numerator;
    Desc.InputFrameRate.Denominator  = pEntry->desc.InputFrameRate.denominator;
    Desc.InputWidth                  = pEntry->desc.InputWidth;
    Desc.InputHeight                 = pEntry->desc.InputHeight;
    Desc.OutputFrameRate.Numerator   = pEntry->desc.OutputFrameRate.numerator;
    Desc.OutputFrameRate.Denominator = pEntry->desc.OutputFrameRate.denominator;
    Desc.OutputWidth                 = pEntry->desc.OutputWidth;
    Desc.OutputHeight                = pEntry->desc.OutputHeight;
    Desc.Usage                       = dxVideoUsage(pEntry->desc.Usage);

    HRESULT hr = pDXDevice->pVideoDevice->CreateVideoProcessorEnumerator(&Desc, &pDXVideoProcessor->pEnum);
    AssertReturn(SUCCEEDED(hr), VERR_NOT_SUPPORTED);

    hr = pDXDevice->pVideoDevice->CreateVideoProcessor(pDXVideoProcessor->pEnum, 0, &pDXVideoProcessor->pVideoProcessor);
    AssertReturn(SUCCEEDED(hr), VERR_NOT_SUPPORTED);

    dxSetupVideoProcessor(pDXDevice, pDXVideoProcessor, pEntry);
    return VINF_SUCCESS;
}


static void dxDestroyVideoProcessor(DXVIDEOPROCESSOR *pDXVideoProcessor)
{
    D3D_RELEASE(pDXVideoProcessor->pEnum);
    D3D_RELEASE(pDXVideoProcessor->pVideoProcessor);
}


static int dxCreateVideoDecoder(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoDecoderId videoDecoderId, VBSVGACOTableDXVideoDecoderEntry const *pEntry)
{
    HRESULT hr;

    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoDevice, VERR_INVALID_STATE);

    DXVIDEODECODER *pDXVideoDecoder = &pDXContext->pBackendDXContext->paVideoDecoder[videoDecoderId];

    D3D11_VIDEO_DECODER_DESC VideoDesc;
    RT_ZERO(VideoDesc);
    memcpy(&VideoDesc.Guid, &pEntry->desc.DecodeProfile, sizeof(GUID));
    VideoDesc.SampleWidth  = pEntry->desc.SampleWidth;
    VideoDesc.SampleHeight = pEntry->desc.SampleHeight;
    VideoDesc.OutputFormat = vmsvgaDXSurfaceFormat2Dxgi(pEntry->desc.OutputFormat);

    D3D11_VIDEO_DECODER_CONFIG Config;
    RT_ZERO(Config);
    memcpy(&Config.guidConfigBitstreamEncryption, &pEntry->config.guidConfigBitstreamEncryption, sizeof(GUID));
    memcpy(&Config.guidConfigMBcontrolEncryption, &pEntry->config.guidConfigMBcontrolEncryption, sizeof(GUID));
    memcpy(&Config.guidConfigResidDiffEncryption, &pEntry->config.guidConfigResidDiffEncryption, sizeof(GUID));
    Config.ConfigBitstreamRaw             = pEntry->config.ConfigBitstreamRaw;
    Config.ConfigMBcontrolRasterOrder     = pEntry->config.ConfigMBcontrolRasterOrder;
    Config.ConfigResidDiffHost            = pEntry->config.ConfigResidDiffHost;
    Config.ConfigSpatialResid8            = pEntry->config.ConfigSpatialResid8;
    Config.ConfigResid8Subtraction        = pEntry->config.ConfigResid8Subtraction;
    Config.ConfigSpatialHost8or9Clipping  = pEntry->config.ConfigSpatialHost8or9Clipping;
    Config.ConfigSpatialResidInterleaved  = pEntry->config.ConfigSpatialResidInterleaved;
    Config.ConfigIntraResidUnsigned       = pEntry->config.ConfigIntraResidUnsigned;
    Config.ConfigResidDiffAccelerator     = pEntry->config.ConfigResidDiffAccelerator;
    Config.ConfigHostInverseScan          = pEntry->config.ConfigHostInverseScan;
    Config.ConfigSpecificIDCT             = pEntry->config.ConfigSpecificIDCT;
    Config.Config4GroupedCoefs            = pEntry->config.Config4GroupedCoefs;
    Config.ConfigMinRenderTargetBuffCount = pEntry->config.ConfigMinRenderTargetBuffCount;
    Config.ConfigDecoderSpecific          = pEntry->config.ConfigDecoderSpecific;

    hr = pDXDevice->pVideoDevice->CreateVideoDecoder(&VideoDesc, &Config, &pDXVideoDecoder->pVideoDecoder);
    AssertReturn(SUCCEEDED(hr), VERR_NOT_SUPPORTED);
    LogFlowFunc(("Using DecodeProfile %RTuuid\n", &VideoDesc.Guid));

    /* COTables are restored from saved state in ascending order. Output View must be created before Decoder. */
    AssertCompile(VBSVGA_COTABLE_VDOV < VBSVGA_COTABLE_VIDEODECODER);
    if (pEntry->vdovId != SVGA3D_INVALID_ID)
    {
        int rc = dxVideoDecoderBeginFrame(pThisCC, pDXContext, videoDecoderId, pEntry->vdovId);
        AssertRC(rc); RT_NOREF(rc);
    }

    return VINF_SUCCESS;
}


static void dxDestroyVideoDecoder(DXVIDEODECODER *pDXVideoDecoder)
{
    D3D_RELEASE(pDXVideoDecoder->pVideoDecoder);
}


/*
 * Backend callbacks.
 */

static DECLCALLBACK(int) vmsvga3dBackVBDXDefineVideoProcessor(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId, VBSVGACOTableDXVideoProcessorEntry const *pEntry)
{
    return dxCreateVideoProcessor(pThisCC, pDXContext, videoProcessorId, pEntry);
}


static DECLCALLBACK(int) vmsvga3dBackVBDXDefineVideoDecoderOutputView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoDecoderOutputViewId videoDecoderOutputViewId, VBSVGACOTableDXVideoDecoderOutputViewEntry const *pEntry)
{
    /* The view is created when it is used: either in BeginFrame or ClearView. */
    RT_NOREF(pThisCC, pDXContext, videoDecoderOutputViewId, pEntry);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXDefineVideoDecoder(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoDecoderId videoDecoderId, VBSVGACOTableDXVideoDecoderEntry const *pEntry)
{
    return dxCreateVideoDecoder(pThisCC, pDXContext, videoDecoderId, pEntry);
}


static DECLCALLBACK(int) vmsvga3dBackVBDXVideoDecoderBeginFrame(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoDecoderId videoDecoderId, VBSVGA3dVideoDecoderOutputViewId videoDecoderOutputViewId)
{
    return dxVideoDecoderBeginFrame(pThisCC, pDXContext, videoDecoderId, videoDecoderOutputViewId);
}


static DECLCALLBACK(int) vmsvga3dBackVBDXVideoDecoderSubmitBuffers(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoDecoderId videoDecoderId, uint32_t cBuffer, VBSVGA3dVideoDecoderBufferDesc const *paBufferDesc)
{
    HRESULT hr;

    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    DXVIDEODECODER *pDXVideoDecoder = &pDXContext->pBackendDXContext->paVideoDecoder[videoDecoderId];

    D3D11_VIDEO_DECODER_BUFFER_DESC *paDesc = (D3D11_VIDEO_DECODER_BUFFER_DESC *)RTMemTmpAllocZ(cBuffer * sizeof(D3D11_VIDEO_DECODER_BUFFER_DESC));
    AssertReturn(paDesc, VERR_NO_MEMORY);

    for (uint32_t i = 0; i < cBuffer; ++i)
    {
        VBSVGA3dVideoDecoderBufferDesc const *s = &paBufferDesc[i];

        PVMSVGA3DSURFACE pSurface;
        int rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, s->sidBuffer, &pSurface);
        ASSERT_GUEST_CONTINUE(RT_SUCCESS(rc));

        uint32_t const cbSurface = pSurface->paMipmapLevels[0].cbSurface;
        ASSERT_GUEST_CONTINUE(   s->dataSize <= cbSurface
                              && s->dataOffset <= cbSurface - s->dataSize);

        D3D11_VIDEO_DECODER_BUFFER_DESC *d = &paDesc[i];
        d->BufferType     = dxVideoDecoderBufferType(s->bufferType);
        d->DataOffset     = 0;
        d->DataSize       = s->dataSize;
        d->FirstMBaddress = s->firstMBaddress;
        d->NumMBsInBuffer = s->numMBsInBuffer;

        UINT DecoderBufferSize;
        void *pDecoderBuffer;
        hr = pDXDevice->pVideoContext->GetDecoderBuffer(pDXVideoDecoder->pVideoDecoder, d->BufferType,
                                                        &DecoderBufferSize, &pDecoderBuffer);
        AssertReturnStmt(SUCCEEDED(hr), RTMemTmpFree(paDesc), VERR_NOT_SUPPORTED);

        ASSERT_GUEST_CONTINUE(DecoderBufferSize >= s->dataSize);

        if (pSurface->pBackendSurface)
        {
            void *pvGuestBuffer;
            uint32_t cbGuestBuffer;
            rc = dxReadBuffer(pDXDevice, pSurface->pBackendSurface->u.pBuffer, s->dataOffset, s->dataSize,
                              &pvGuestBuffer, &cbGuestBuffer);
            AssertRC(rc);
            if (RT_SUCCESS(rc))
            {
                memcpy(pDecoderBuffer, pvGuestBuffer, cbGuestBuffer);
                RTMemFree(pvGuestBuffer);
            }
        }
        else
            memcpy(pDecoderBuffer, (uint8_t *)pSurface->paMipmapLevels[0].pSurfaceData + s->dataOffset, s->dataSize);

        hr = pDXDevice->pVideoContext->ReleaseDecoderBuffer(pDXVideoDecoder->pVideoDecoder, d->BufferType);
        AssertReturnStmt(SUCCEEDED(hr), RTMemTmpFree(paDesc), VERR_NOT_SUPPORTED);
    }

    hr = pDXDevice->pVideoContext->SubmitDecoderBuffers(pDXVideoDecoder->pVideoDecoder, cBuffer, paDesc);
    AssertReturnStmt(SUCCEEDED(hr), RTMemTmpFree(paDesc), VERR_NOT_SUPPORTED);

    RTMemTmpFree(paDesc);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXVideoDecoderEndFrame(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoDecoderId videoDecoderId)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    DXVIDEODECODER *pDXVideoDecoder = &pDXContext->pBackendDXContext->paVideoDecoder[videoDecoderId];

    HRESULT hr = pDXDevice->pVideoContext->DecoderEndFrame(pDXVideoDecoder->pVideoDecoder);
    AssertReturn(SUCCEEDED(hr), VERR_NOT_SUPPORTED);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXDefineVideoProcessorInputView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorInputViewId videoProcessorInputViewId, VBSVGACOTableDXVideoProcessorInputViewEntry const *pEntry)
{
    /* The view is created when it is used: either in VideoProcessorBlt or ClearView. */
    RT_NOREF(pThisCC, pDXContext, videoProcessorInputViewId, pEntry);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXDefineVideoProcessorOutputView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorOutputViewId videoProcessorOutputViewId, VBSVGACOTableDXVideoProcessorOutputViewEntry const *pEntry)
{
    /* The view is created when it is used: either in VideoProcessorBlt or ClearView. */
    RT_NOREF(pThisCC, pDXContext, videoProcessorOutputViewId, pEntry);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXVideoProcessorBlt(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId, VBSVGA3dVideoProcessorOutputViewId videoProcessorOutputViewId,
                                                           uint32_t OutputFrame, uint32_t StreamCount, VBSVGA3dVideoProcessorStream const *pVideoProcessorStreams)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    DXVIDEOPROCESSOR *pDXVideoProcessor = &pDXContext->pBackendDXContext->paVideoProcessor[videoProcessorId];

    DXVIEW *pVPOView;
    int rc = dxEnsureVideoProcessorOutputView(pThisCC, pDXContext, videoProcessorOutputViewId, &pVPOView);
    AssertRCReturn(rc, rc);

    uint32_t cbStreams = StreamCount * sizeof(D3D11_VIDEO_PROCESSOR_STREAM);

    /* ID3D11VideoProcessorInputView arrays for past and future frames. */
    VBSVGA3dVideoProcessorStream const *pVPS = pVideoProcessorStreams;
    for (uint32_t i = 0; i < StreamCount; ++i)
    {
        uint32_t const cIds = (pVPS->StereoFormatSeparate == 0 ? 1 : 2) * (pVPS->PastFrames + 1 + pVPS->FutureFrames);

        uint32_t const cPastFutureViews = (pVPS->StereoFormatSeparate == 0 ? 1 : 2) * (pVPS->PastFrames + pVPS->FutureFrames);
        cbStreams += cPastFutureViews * sizeof(ID3D11VideoProcessorInputView *);

        pVPS = (VBSVGA3dVideoProcessorStream *)((uint8_t *)&pVPS[1] + cIds * sizeof(VBSVGA3dVideoProcessorInputViewId));
    }

    D3D11_VIDEO_PROCESSOR_STREAM *paStreams = (D3D11_VIDEO_PROCESSOR_STREAM *)RTMemTmpAllocZ(cbStreams);
    AssertReturn(paStreams, VERR_NO_MEMORY);
    ID3D11VideoProcessorInputView **ppSurfaces = (ID3D11VideoProcessorInputView **)&paStreams[StreamCount];

    pVPS = pVideoProcessorStreams;
    for (uint32_t i = 0; i < StreamCount; ++i)
    {
        D3D11_VIDEO_PROCESSOR_STREAM *d = &paStreams[i];
        d->Enable            = pVPS->Enable;
        d->OutputIndex       = pVPS->OutputIndex;
        d->InputFrameOrField = pVPS->InputFrameOrField;
        d->PastFrames        = pVPS->PastFrames;
        d->FutureFrames      = pVPS->FutureFrames;

        /*
         * Fetch input frames.
         */
        uint32_t const cIds = (pVPS->StereoFormatSeparate == 0 ? 1 : 2) * (pVPS->PastFrames + 1 + pVPS->FutureFrames);
        VBSVGA3dVideoProcessorInputViewId const *pId = (VBSVGA3dVideoProcessorInputViewId *)&pVPS[1];
        DXVIEW *pVPIView;

        /* Past frames. */
        if (pVPS->PastFrames)
        {
            DEBUG_BREAKPOINT_TEST();
            d->ppPastSurfaces = ppSurfaces;
            for (UINT j = 0; j < pVPS->PastFrames; ++j, ++ppSurfaces)
            {
                rc = dxEnsureVideoProcessorInputView(pThisCC, pDXContext, *pId++, &pVPIView);
                AssertRCReturnStmt(rc, RTMemTmpFree(paStreams), rc);
                d->ppPastSurfaces[j] = pVPIView->u.pVideoProcessorInputView;
            }
        }

        /* CurrentFrame */
        rc = dxEnsureVideoProcessorInputView(pThisCC, pDXContext, *pId++, &pVPIView);
        AssertRCReturnStmt(rc, RTMemTmpFree(paStreams), rc);
        d->pInputSurface = pVPIView->u.pVideoProcessorInputView;

        /* Future frames. */
        if (pVPS->FutureFrames)
        {
            d->ppFutureSurfaces = ppSurfaces;
            for (UINT j = 0; j < pVPS->FutureFrames; ++j, ++ppSurfaces)
            {
                rc = dxEnsureVideoProcessorInputView(pThisCC, pDXContext, *pId++, &pVPIView);
                AssertRCReturnStmt(rc, RTMemTmpFree(paStreams), rc);
                d->ppFutureSurfaces[j] = pVPIView->u.pVideoProcessorInputView;
            }
        }

        /* Right frames for stereo. */
        if (pVPS->StereoFormatSeparate)
        {
            /* Past frames. */
            if (pVPS->PastFrames)
            {
                 d->ppPastSurfacesRight = ppSurfaces;
                 for (UINT j = 0; j < pVPS->PastFrames; ++j, ++ppSurfaces)
                 {
                     rc = dxEnsureVideoProcessorInputView(pThisCC, pDXContext, *pId++, &pVPIView);
                     AssertRCReturnStmt(rc, RTMemTmpFree(paStreams), rc);
                     d->ppPastSurfacesRight[j] = pVPIView->u.pVideoProcessorInputView;
                 }
            }

            /* CurrentFrame */
            rc = dxEnsureVideoProcessorInputView(pThisCC, pDXContext, *pId++, &pVPIView);
            AssertRCReturnStmt(rc, RTMemTmpFree(paStreams), rc);
            d->pInputSurfaceRight = pVPIView->u.pVideoProcessorInputView;

            /* Future frames. */
            if (pVPS->FutureFrames)
            {
                d->ppFutureSurfacesRight = ppSurfaces;
                for (UINT j = 0; j < pVPS->FutureFrames; ++j, ++ppSurfaces)
                {
                    rc = dxEnsureVideoProcessorInputView(pThisCC, pDXContext, *pId++, &pVPIView);
                    AssertRCReturnStmt(rc, RTMemTmpFree(paStreams), rc);
                    d->ppFutureSurfacesRight[j] = pVPIView->u.pVideoProcessorInputView;
                }
            }
        }

        pVPS = (VBSVGA3dVideoProcessorStream *)((uint8_t *)&pVPS[1] + cIds * sizeof(VBSVGA3dVideoProcessorInputViewId));
    }

    HRESULT hr = pDXDevice->pVideoContext->VideoProcessorBlt(pDXVideoProcessor->pVideoProcessor, pVPOView->u.pVideoProcessorOutputView,
                                                             OutputFrame, StreamCount, paStreams);
    RTMemTmpFree(paStreams);
    AssertReturn(SUCCEEDED(hr), VERR_NOT_SUPPORTED);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXDestroyVideoDecoder(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoDecoderId videoDecoderId)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXVIDEODECODER *pDXVideoDecoder = &pDXContext->pBackendDXContext->paVideoDecoder[videoDecoderId];
    dxDestroyVideoDecoder(pDXVideoDecoder);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXDestroyVideoDecoderOutputView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoDecoderOutputViewId videoDecoderOutputViewId)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXVIEW *pDXView = &pDXContext->pBackendDXContext->paVideoDecoderOutputView[videoDecoderOutputViewId];
    dxViewDestroy(pDXView);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXDestroyVideoProcessor(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXVIDEOPROCESSOR *pDXVideoProcessor = &pDXContext->pBackendDXContext->paVideoProcessor[videoProcessorId];
    dxDestroyVideoProcessor(pDXVideoProcessor);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXDestroyVideoProcessorInputView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorInputViewId videoProcessorInputViewId)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXVIEW *pDXView = &pDXContext->pBackendDXContext->paVideoProcessorInputView[videoProcessorInputViewId];
    dxViewDestroy(pDXView);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXDestroyVideoProcessorOutputView(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorOutputViewId videoProcessorOutputViewId)
{
    PVMSVGA3DBACKEND pBackend = pThisCC->svga.p3dState->pBackend;
    RT_NOREF(pBackend);

    DXVIEW *pDXView = &pDXContext->pBackendDXContext->paVideoProcessorOutputView[videoProcessorOutputViewId];
    dxViewDestroy(pDXView);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXVideoProcessorSetOutputTargetRect(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId, uint8 enable, SVGASignedRect const &outputRect)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    DXVIDEOPROCESSOR *pDXVideoProcessor = &pDXContext->pBackendDXContext->paVideoProcessor[videoProcessorId];
    dxVideoProcessorSetOutputTargetRect(pDXDevice, pDXVideoProcessor, enable, outputRect);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXVideoProcessorSetOutputBackgroundColor(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId, uint32 YCbCr, VBSVGA3dVideoColor const &color)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    DXVIDEOPROCESSOR *pDXVideoProcessor = &pDXContext->pBackendDXContext->paVideoProcessor[videoProcessorId];
    dxVideoProcessorSetOutputBackgroundColor(pDXDevice, pDXVideoProcessor, YCbCr, color);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXVideoProcessorSetOutputColorSpace(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId, VBSVGA3dVideoProcessorColorSpace const &colorSpace)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    DXVIDEOPROCESSOR *pDXVideoProcessor = &pDXContext->pBackendDXContext->paVideoProcessor[videoProcessorId];
    dxVideoProcessorSetOutputColorSpace(pDXDevice, pDXVideoProcessor, colorSpace);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXVideoProcessorSetOutputAlphaFillMode(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId, VBSVGA3dVideoProcessorAlphaFillMode fillMode, uint32 streamIndex)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    DXVIDEOPROCESSOR *pDXVideoProcessor = &pDXContext->pBackendDXContext->paVideoProcessor[videoProcessorId];
    dxVideoProcessorSetOutputAlphaFillMode(pDXDevice, pDXVideoProcessor, fillMode, streamIndex);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXVideoProcessorSetOutputConstriction(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId, uint32 enabled, uint32 width, uint32 height)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    DXVIDEOPROCESSOR *pDXVideoProcessor = &pDXContext->pBackendDXContext->paVideoProcessor[videoProcessorId];
    dxVideoProcessorSetOutputConstriction(pDXDevice, pDXVideoProcessor, enabled, width, height);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXVideoProcessorSetOutputStereoMode(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId, uint32 enable)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    DXVIDEOPROCESSOR *pDXVideoProcessor = &pDXContext->pBackendDXContext->paVideoProcessor[videoProcessorId];
    dxVideoProcessorSetOutputStereoMode(pDXDevice, pDXVideoProcessor, enable);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXVideoProcessorSetStreamFrameFormat(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId, uint32 streamIndex, VBSVGA3dVideoFrameFormat format)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    DXVIDEOPROCESSOR *pDXVideoProcessor = &pDXContext->pBackendDXContext->paVideoProcessor[videoProcessorId];
    dxVideoProcessorSetStreamFrameFormat(pDXDevice, pDXVideoProcessor, streamIndex, format);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXVideoProcessorSetStreamColorSpace(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId, uint32 streamIndex, VBSVGA3dVideoProcessorColorSpace colorSpace)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    DXVIDEOPROCESSOR *pDXVideoProcessor = &pDXContext->pBackendDXContext->paVideoProcessor[videoProcessorId];
    dxVideoProcessorSetStreamColorSpace(pDXDevice, pDXVideoProcessor, streamIndex, colorSpace);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXVideoProcessorSetStreamOutputRate(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId,
                                                                           uint32 streamIndex, VBSVGA3dVideoProcessorOutputRate outputRate, uint32 repeatFrame, SVGA3dFraction64 const &customRate)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    DXVIDEOPROCESSOR *pDXVideoProcessor = &pDXContext->pBackendDXContext->paVideoProcessor[videoProcessorId];
    dxVideoProcessorSetStreamOutputRate(pDXDevice, pDXVideoProcessor, streamIndex, outputRate, repeatFrame, customRate);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXVideoProcessorSetStreamSourceRect(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId,
                                                                           uint32 streamIndex, uint32 enable, SVGASignedRect const &sourceRect)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    DXVIDEOPROCESSOR *pDXVideoProcessor = &pDXContext->pBackendDXContext->paVideoProcessor[videoProcessorId];
    dxVideoProcessorSetStreamSourceRect(pDXDevice, pDXVideoProcessor, streamIndex, enable, sourceRect);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXVideoProcessorSetStreamDestRect(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId,
                                                                         uint32 streamIndex, uint32 enable, SVGASignedRect const &destRect)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    DXVIDEOPROCESSOR *pDXVideoProcessor = &pDXContext->pBackendDXContext->paVideoProcessor[videoProcessorId];
    dxVideoProcessorSetStreamDestRect(pDXDevice, pDXVideoProcessor, streamIndex, enable, destRect);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXVideoProcessorSetStreamAlpha(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId,
                                                                      uint32 streamIndex, uint32 enable, float alpha)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    DXVIDEOPROCESSOR *pDXVideoProcessor = &pDXContext->pBackendDXContext->paVideoProcessor[videoProcessorId];
    dxVideoProcessorSetStreamAlpha(pDXDevice, pDXVideoProcessor, streamIndex, enable, alpha);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXVideoProcessorSetStreamPalette(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId,
                                                                        uint32 streamIndex, uint32_t cEntries, uint32_t const *paEntries)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    DXVIDEOPROCESSOR *pDXVideoProcessor = &pDXContext->pBackendDXContext->paVideoProcessor[videoProcessorId];
    dxVideoProcessorSetStreamPalette(pDXDevice, pDXVideoProcessor, streamIndex, cEntries, paEntries);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXVideoProcessorSetStreamPixelAspectRatio(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId,
                                                                                 uint32 streamIndex, uint32 enable, SVGA3dFraction64 const &sourceRatio, SVGA3dFraction64 const &destRatio)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    DXVIDEOPROCESSOR *pDXVideoProcessor = &pDXContext->pBackendDXContext->paVideoProcessor[videoProcessorId];
    dxVideoProcessorSetStreamPixelAspectRatio(pDXDevice, pDXVideoProcessor, streamIndex, enable, sourceRatio, destRatio);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXVideoProcessorSetStreamLumaKey(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId,
                                                                        uint32 streamIndex, uint32 enable, float lower, float upper)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    DXVIDEOPROCESSOR *pDXVideoProcessor = &pDXContext->pBackendDXContext->paVideoProcessor[videoProcessorId];
    dxVideoProcessorSetStreamLumaKey(pDXDevice, pDXVideoProcessor, streamIndex, enable, lower, upper);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXVideoProcessorSetStreamStereoFormat(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId,
                                                                             uint32 streamIndex, uint32 enable, VBSVGA3dVideoProcessorStereoFormat stereoFormat,
                                                                             uint8 leftViewFrame0, uint8 baseViewFrame0, VBSVGA3dVideoProcessorStereoFlipMode flipMode, int32 monoOffset)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    DXVIDEOPROCESSOR *pDXVideoProcessor = &pDXContext->pBackendDXContext->paVideoProcessor[videoProcessorId];
    dxVideoProcessorSetStreamStereoFormat(pDXDevice, pDXVideoProcessor, streamIndex, enable, stereoFormat,
                                          leftViewFrame0, baseViewFrame0, flipMode, monoOffset);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXVideoProcessorSetStreamAutoProcessingMode(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId,
                                                                                   uint32 streamIndex, uint32 enable)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    DXVIDEOPROCESSOR *pDXVideoProcessor = &pDXContext->pBackendDXContext->paVideoProcessor[videoProcessorId];
    dxVideoProcessorSetStreamAutoProcessingMode(pDXDevice, pDXVideoProcessor, streamIndex, enable);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXVideoProcessorSetStreamFilter(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId,
                                                                       uint32 streamIndex, uint32 enable, VBSVGA3dVideoProcessorFilter filter, int32 level)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    DXVIDEOPROCESSOR *pDXVideoProcessor = &pDXContext->pBackendDXContext->paVideoProcessor[videoProcessorId];
    dxVideoProcessorSetStreamFilter(pDXDevice, pDXVideoProcessor, streamIndex, enable, filter, level);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXVideoProcessorSetStreamRotation(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorId videoProcessorId,
                                                                         uint32 streamIndex, uint32 enable, VBSVGA3dVideoProcessorRotation rotation)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    DXVIDEOPROCESSOR *pDXVideoProcessor = &pDXContext->pBackendDXContext->paVideoProcessor[videoProcessorId];
    dxVideoProcessorSetStreamRotation(pDXDevice, pDXVideoProcessor, streamIndex, enable, rotation);
    return VINF_SUCCESS;
}


static int dxGetVideoCapDecodeProfile(PVGASTATECC pThisCC, DXDEVICE *pDXDevice, void *pvData, uint32 cbData, uint32 *pcbOut)
{
    VBSVGA3dDecodeProfileInfo *paDecodeProfileInfo = (VBSVGA3dDecodeProfileInfo *)pvData;

    UINT ProfileCount = pDXDevice->pVideoDevice->GetVideoDecoderProfileCount();
    ProfileCount = RT_MIN(ProfileCount, cbData / sizeof(paDecodeProfileInfo[0]));

    RT_NOREF(pThisCC);

    for (UINT i = 0; i < ProfileCount; ++i)
    {
        VBSVGA3dDecodeProfileInfo *d = &paDecodeProfileInfo[i];

        /* GUID and VBSVGA3dGuid are identical. */
        GUID *pGuid = (GUID *)&d->DecodeProfile;
        HRESULT hr = pDXDevice->pVideoDevice->GetVideoDecoderProfile(i, pGuid);
        Assert(SUCCEEDED(hr));
        if (SUCCEEDED(hr))
        {
            struct
            {
                DXGI_FORMAT format;
                uint8 *pSupported;
            } aFormats[] =
            {
                { DXGI_FORMAT_AYUV, &d->fAYUV },
                { DXGI_FORMAT_NV12, &d->fNV12 },
                { DXGI_FORMAT_YUY2, &d->fYUY2 },
            };

            for (unsigned idxFormat = 0; idxFormat < RT_ELEMENTS(aFormats); ++idxFormat)
            {
                BOOL Supported = FALSE;
                pDXDevice->pVideoDevice->CheckVideoDecoderFormat(pGuid, aFormats[idxFormat].format, &Supported);
                *aFormats[idxFormat].pSupported = RT_BOOL(Supported);
            }
        }

        if (FAILED(hr))
            RT_ZERO(*d);
    }

    *pcbOut = ProfileCount * sizeof(VBSVGA3dDecodeProfileInfo);
    return VINF_SUCCESS;
}


static int dxGetVideoCapDecodeConfig(DXDEVICE *pDXDevice, void *pvData, uint32 cbData, uint32 *pcbOut)
{
    ASSERT_GUEST_RETURN(cbData >= sizeof(VBSVGA3dVideoDecoderDesc), VERR_INVALID_PARAMETER);
    VBSVGA3dDecodeConfigInfo *pConfigInfo = (VBSVGA3dDecodeConfigInfo *)pvData;

    D3D11_VIDEO_DECODER_DESC Desc;
    RT_ZERO(Desc);
    memcpy(&Desc.Guid, &pConfigInfo->desc.DecodeProfile, sizeof(GUID));
    Desc.SampleWidth  = pConfigInfo->desc.SampleWidth;
    Desc.SampleHeight = pConfigInfo->desc.SampleHeight;
    Desc.OutputFormat = vmsvgaDXSurfaceFormat2Dxgi(pConfigInfo->desc.OutputFormat);

    UINT ConfigCount;
    HRESULT hr = pDXDevice->pVideoDevice->GetVideoDecoderConfigCount(&Desc, &ConfigCount);
    if (FAILED(hr))
        ConfigCount = 0;
    ConfigCount = RT_MIN(ConfigCount, (cbData - sizeof(pConfigInfo->desc)) / sizeof(pConfigInfo->aConfig[0]));

    UINT cConfigOut = 0;
    for (UINT i = 0; i < ConfigCount; ++i)
    {
        D3D11_VIDEO_DECODER_CONFIG Config;
        hr = pDXDevice->pVideoDevice->GetVideoDecoderConfig(&Desc, i, &Config);
        Assert(SUCCEEDED(hr));
        if (SUCCEEDED(hr))
        {
            /* Filter out configs with encryption. */
            static GUID const NoEncrypt = { 0x1b81beD0, 0xa0c7,0x11d3,{0xb9,0x84,0x00,0xc0,0x4f,0x2e,0x73,0xc5} };
            if (   memcmp(&NoEncrypt, &Config.guidConfigBitstreamEncryption, sizeof(GUID)) == 0
                && memcmp(&NoEncrypt, &Config.guidConfigMBcontrolEncryption, sizeof(GUID)) == 0
                && memcmp(&NoEncrypt, &Config.guidConfigResidDiffEncryption, sizeof(GUID)) == 0)
            {
                VBSVGA3dVideoDecoderConfig *d = &pConfigInfo->aConfig[cConfigOut++];

                memcpy(&d->guidConfigBitstreamEncryption, &Config.guidConfigBitstreamEncryption, sizeof(VBSVGA3dGuid));
                memcpy(&d->guidConfigMBcontrolEncryption, &Config.guidConfigMBcontrolEncryption, sizeof(VBSVGA3dGuid));
                memcpy(&d->guidConfigResidDiffEncryption, &Config.guidConfigResidDiffEncryption, sizeof(VBSVGA3dGuid));
                d->ConfigBitstreamRaw             = Config.ConfigBitstreamRaw;
                d->ConfigMBcontrolRasterOrder     = Config.ConfigMBcontrolRasterOrder;
                d->ConfigResidDiffHost            = Config.ConfigResidDiffHost;
                d->ConfigSpatialResid8            = Config.ConfigSpatialResid8;
                d->ConfigResid8Subtraction        = Config.ConfigResid8Subtraction;
                d->ConfigSpatialHost8or9Clipping  = Config.ConfigSpatialHost8or9Clipping;
                d->ConfigSpatialResidInterleaved  = Config.ConfigSpatialResidInterleaved;
                d->ConfigIntraResidUnsigned       = Config.ConfigIntraResidUnsigned;
                d->ConfigResidDiffAccelerator     = Config.ConfigResidDiffAccelerator;
                d->ConfigHostInverseScan          = Config.ConfigHostInverseScan;
                d->ConfigSpecificIDCT             = Config.ConfigSpecificIDCT;
                d->Config4GroupedCoefs            = Config.Config4GroupedCoefs;
                d->ConfigMinRenderTargetBuffCount = Config.ConfigMinRenderTargetBuffCount;
                d->ConfigDecoderSpecific          = Config.ConfigDecoderSpecific;
            }
        }
    }

    //DEBUG_BREAKPOINT_TEST();
    *pcbOut = sizeof(VBSVGA3dVideoDecoderDesc) + cConfigOut * sizeof(VBSVGA3dVideoDecoderConfig);
    return VINF_SUCCESS;
}


static int dxGetVideoCapProcessorEnum(DXDEVICE *pDXDevice, void *pvData, uint32 cbData, uint32 *pcbOut)
{
    ASSERT_GUEST_RETURN(cbData >= sizeof(VBSVGA3dProcessorEnumInfo), VERR_INVALID_PARAMETER);

    VBSVGA3dProcessorEnumInfo *pInfo = (VBSVGA3dProcessorEnumInfo *)pvData;
    RT_ZERO(pInfo->info);

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC ContentDesc;
    RT_ZERO(ContentDesc);
    ContentDesc.InputFrameFormat            = dxVideoFrameFormat(pInfo->desc.InputFrameFormat);
    ContentDesc.InputFrameRate.Numerator    = pInfo->desc.InputFrameRate.numerator;
    ContentDesc.InputFrameRate.Denominator  = pInfo->desc.InputFrameRate.denominator;
    ContentDesc.InputWidth                  = pInfo->desc.InputWidth;
    ContentDesc.InputHeight                 = pInfo->desc.InputHeight;
    ContentDesc.OutputFrameRate.Numerator   = pInfo->desc.OutputFrameRate.numerator;
    ContentDesc.OutputFrameRate.Denominator = pInfo->desc.OutputFrameRate.denominator;
    ContentDesc.OutputWidth                 = pInfo->desc.OutputWidth;
    ContentDesc.OutputHeight                = pInfo->desc.OutputHeight;
    ContentDesc.Usage                       = dxVideoUsage(pInfo->desc.Usage);

    ID3D11VideoProcessorEnumerator *pEnum;
    HRESULT hr = pDXDevice->pVideoDevice->CreateVideoProcessorEnumerator(&ContentDesc, &pEnum);
    AssertReturn(SUCCEEDED(hr), VERR_NOT_SUPPORTED);

    struct
    {
        DXGI_FORMAT format;
        uint8 *pFlags;
    } aFormats[] =
    {
        { DXGI_FORMAT_R8_UNORM,            &pInfo->info.fR8_UNORM },
        { DXGI_FORMAT_R16_UNORM,           &pInfo->info.fR16_UNORM },
        { DXGI_FORMAT_NV12,                &pInfo->info.fNV12 },
        { DXGI_FORMAT_YUY2,                &pInfo->info.fYUY2 },
        { DXGI_FORMAT_R16G16B16A16_FLOAT,  &pInfo->info.fR16G16B16A16_FLOAT },
        { DXGI_FORMAT_B8G8R8X8_UNORM,      &pInfo->info.fB8G8R8X8_UNORM },
        { DXGI_FORMAT_B8G8R8A8_UNORM,      &pInfo->info.fB8G8R8A8_UNORM },
        { DXGI_FORMAT_R8G8B8A8_UNORM,      &pInfo->info.fR8G8B8A8_UNORM },
        { DXGI_FORMAT_R10G10B10A2_UNORM,   &pInfo->info.fR10G10B10A2_UNORM },
        { DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM, &pInfo->info.fR10G10B10_XR_BIAS_A2_UNORM },
        { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, &pInfo->info.fR8G8B8A8_UNORM_SRGB },
        { DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, &pInfo->info.fB8G8R8A8_UNORM_SRGB },
    };

    for (unsigned idxFormat = 0; idxFormat < RT_ELEMENTS(aFormats); ++idxFormat)
    {
        UINT Flags = 0;
        hr = pEnum->CheckVideoProcessorFormat(aFormats[idxFormat].format, &Flags);
        if (SUCCEEDED(hr))
            *aFormats[idxFormat].pFlags = Flags;
    }

    D3D11_VIDEO_PROCESSOR_CAPS Caps;
    hr = pEnum->GetVideoProcessorCaps(&Caps);
    if (SUCCEEDED(hr))
    {
        pInfo->info.Caps.DeviceCaps              = Caps.DeviceCaps;
        pInfo->info.Caps.FeatureCaps             = Caps.FeatureCaps;
        pInfo->info.Caps.FilterCaps              = Caps.FilterCaps;
        pInfo->info.Caps.InputFormatCaps         = Caps.InputFormatCaps;
        pInfo->info.Caps.AutoStreamCaps          = Caps.AutoStreamCaps;
        pInfo->info.Caps.StereoCaps              = Caps.StereoCaps;
        pInfo->info.Caps.RateConversionCapsCount = RT_MIN(Caps.RateConversionCapsCount, VBSVGA3D_MAX_VIDEO_RATE_CONVERSION_CAPS);
        pInfo->info.Caps.MaxInputStreams         = RT_MIN(Caps.MaxInputStreams, VBSVGA3D_MAX_VIDEO_STREAMS);
        pInfo->info.Caps.MaxStreamStates         = RT_MIN(Caps.MaxStreamStates, VBSVGA3D_MAX_VIDEO_STREAMS);
    }

    D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS RateCaps;
    hr = pEnum->GetVideoProcessorRateConversionCaps(0, &RateCaps);
    if (SUCCEEDED(hr))
    {
        pInfo->info.RateCaps.PastFrames      = RateCaps.PastFrames;
        pInfo->info.RateCaps.FutureFrames    = RateCaps.FutureFrames;
        pInfo->info.RateCaps.ProcessorCaps   = RateCaps.ProcessorCaps;
        pInfo->info.RateCaps.ITelecineCaps   = RateCaps.ITelecineCaps;
        pInfo->info.RateCaps.CustomRateCount = RT_MIN(RateCaps.CustomRateCount, VBSVGA3D_MAX_VIDEO_CUSTOM_RATE_CAPS);
    }

    for (unsigned i = 0; i < pInfo->info.RateCaps.CustomRateCount; ++i)
    {
        D3D11_VIDEO_PROCESSOR_CUSTOM_RATE Rate;
        hr = pEnum->GetVideoProcessorCustomRate(0, i, &Rate);
        if (SUCCEEDED(hr))
        {
            pInfo->info.aCustomRateCaps[i].CustomRate.numerator   = Rate.CustomRate.Numerator;
            pInfo->info.aCustomRateCaps[i].CustomRate.denominator = Rate.CustomRate.Denominator;
            pInfo->info.aCustomRateCaps[i].OutputFrames           = Rate.OutputFrames;
            pInfo->info.aCustomRateCaps[i].InputInterlaced        = Rate.InputInterlaced;
            pInfo->info.aCustomRateCaps[i].InputFramesOrFields    = Rate.InputFramesOrFields;
        }
    }

    for (unsigned i = 0; i < VBSVGA3D_VP_MAX_FILTER_COUNT; ++i)
    {
        if (pInfo->info.Caps.FilterCaps & (1 << i))
        {
            D3D11_VIDEO_PROCESSOR_FILTER_RANGE Range;
            hr = pEnum->GetVideoProcessorFilterRange((D3D11_VIDEO_PROCESSOR_FILTER)i, &Range);
            if (SUCCEEDED(hr))
            {
                pInfo->info.aFilterRange[i].Minimum = Range.Minimum;
                pInfo->info.aFilterRange[i].Maximum = Range.Maximum;
                pInfo->info.aFilterRange[i].Default = Range.Default;
                pInfo->info.aFilterRange[i].Multiplier = Range.Multiplier;
            }
        }
    }

    //DEBUG_BREAKPOINT_TEST();
    D3D_RELEASE(pEnum);

    *pcbOut = sizeof(VBSVGA3dProcessorEnumInfo);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXGetVideoCapability(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoCapability capability, void *pvData, uint32 cbData, uint32 *pcbOut)
{
    RT_NOREF(pDXContext);

    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pVideoContext, VERR_INVALID_STATE);

    switch (capability)
    {
        case VBSVGA3D_VIDEO_CAPABILITY_DECODE_PROFILE:
            return dxGetVideoCapDecodeProfile(pThisCC, pDXDevice, pvData, cbData, pcbOut);
        case VBSVGA3D_VIDEO_CAPABILITY_DECODE_CONFIG:
            return dxGetVideoCapDecodeConfig(pDXDevice, pvData, cbData, pcbOut);
        case VBSVGA3D_VIDEO_CAPABILITY_PROCESSOR_ENUM:
            return dxGetVideoCapProcessorEnum(pDXDevice, pvData, cbData, pcbOut);
        default:
            break;
    }

    return VERR_NOT_SUPPORTED;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXClearUAV(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, SVGA3dUAViewId viewId,
                                                  SVGA3dRGBAFloat const *pColor, uint32_t cRect, SVGASignedRect const *paRect)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pDevice, VERR_INVALID_STATE);

    DXVIEW *pDXView;
    int rc = dxEnsureUnorderedAccessView(pThisCC, pDXContext, viewId, &pDXView);
    AssertRCReturn(rc, rc);

    pDXDevice->pImmediateContext->ClearView(pDXView->u.pView, pColor->value, (D3D11_RECT *)paRect, cRect);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXClearVDOV(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoDecoderOutputViewId viewId,
                                                   SVGA3dRGBAFloat const *pColor, uint32_t cRect, SVGASignedRect const *paRect)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pDevice, VERR_INVALID_STATE);

    DXVIEW *pDXView;
    int rc = dxEnsureVideoDecoderOutputView(pThisCC, pDXContext, viewId, &pDXView);
    AssertRCReturn(rc, rc);

    pDXDevice->pImmediateContext->ClearView(pDXView->u.pView, pColor->value, (D3D11_RECT *)paRect, cRect);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXClearVPIV(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorInputViewId viewId,
                                                   SVGA3dRGBAFloat const *pColor, uint32_t cRect, SVGASignedRect const *paRect)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pDevice, VERR_INVALID_STATE);

    DXVIEW *pDXView;
    int rc = dxEnsureVideoProcessorInputView(pThisCC, pDXContext, viewId, &pDXView);
    AssertRCReturn(rc, rc);

    pDXDevice->pImmediateContext->ClearView(pDXView->u.pView, pColor->value, (D3D11_RECT *)paRect, cRect);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackVBDXClearVPOV(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, VBSVGA3dVideoProcessorOutputViewId viewId,
                                                   SVGA3dRGBAFloat const *pColor, uint32_t cRect, SVGASignedRect const *paRect)
{
    DXDEVICE *pDXDevice = dxDeviceGet(pThisCC->svga.p3dState);
    AssertReturn(pDXDevice->pDevice, VERR_INVALID_STATE);

    DXVIEW *pDXView;
    int rc = dxEnsureVideoProcessorOutputView(pThisCC, pDXContext, viewId, &pDXView);
    AssertRCReturn(rc, rc);

    pDXDevice->pImmediateContext->ClearView(pDXView->u.pView, pColor->value, (D3D11_RECT *)paRect, cRect);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXLoadState(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM)
{
    RT_NOREF(pThisCC);
    uint32_t u32;
    int rc;

    rc = pHlp->pfnSSMGetU32(pSSM, &u32);
    AssertLogRelRCReturn(rc, rc);
    AssertLogRelRCReturn(u32 == pDXContext->pBackendDXContext->cShader, VERR_INVALID_STATE);

    for (uint32_t i = 0; i < pDXContext->pBackendDXContext->cShader; ++i)
    {
        DXSHADER *pDXShader = &pDXContext->pBackendDXContext->paShader[i];

        rc = pHlp->pfnSSMGetU32(pSSM, &u32);
        AssertLogRelRCReturn(rc, rc);
        AssertLogRelReturn((SVGA3dShaderType)u32 == pDXShader->enmShaderType, VERR_INVALID_STATE);

        if (pDXShader->enmShaderType == SVGA3D_SHADERTYPE_INVALID)
            continue;

        pHlp->pfnSSMGetU32(pSSM, &pDXShader->soid);

        pHlp->pfnSSMGetU32(pSSM, &u32);
        pDXShader->shaderInfo.enmProgramType = (VGPU10_PROGRAM_TYPE)u32;

        rc = pHlp->pfnSSMGetU32(pSSM, &pDXShader->shaderInfo.cbBytecode);
        AssertLogRelRCReturn(rc, rc);
        AssertLogRelReturn(pDXShader->shaderInfo.cbBytecode <= 2 * SVGA3D_MAX_SHADER_MEMORY_BYTES, VERR_INVALID_STATE);

        if (pDXShader->shaderInfo.cbBytecode)
        {
            pDXShader->shaderInfo.pvBytecode = RTMemAlloc(pDXShader->shaderInfo.cbBytecode);
            AssertPtrReturn(pDXShader->shaderInfo.pvBytecode, VERR_NO_MEMORY);
            pHlp->pfnSSMGetMem(pSSM, pDXShader->shaderInfo.pvBytecode, pDXShader->shaderInfo.cbBytecode);
        }

        rc = pHlp->pfnSSMGetU32(pSSM, &pDXShader->shaderInfo.cInputSignature);
        AssertLogRelRCReturn(rc, rc);
        AssertLogRelReturn(pDXShader->shaderInfo.cInputSignature <= 32, VERR_INVALID_STATE);
        if (pDXShader->shaderInfo.cInputSignature)
            pHlp->pfnSSMGetMem(pSSM, pDXShader->shaderInfo.aInputSignature, pDXShader->shaderInfo.cInputSignature * sizeof(SVGA3dDXSignatureEntry));

        rc = pHlp->pfnSSMGetU32(pSSM, &pDXShader->shaderInfo.cOutputSignature);
        AssertLogRelRCReturn(rc, rc);
        AssertLogRelReturn(pDXShader->shaderInfo.cOutputSignature <= 32, VERR_INVALID_STATE);
        if (pDXShader->shaderInfo.cOutputSignature)
            pHlp->pfnSSMGetMem(pSSM, pDXShader->shaderInfo.aOutputSignature, pDXShader->shaderInfo.cOutputSignature * sizeof(SVGA3dDXSignatureEntry));

        rc = pHlp->pfnSSMGetU32(pSSM, &pDXShader->shaderInfo.cPatchConstantSignature);
        AssertLogRelRCReturn(rc, rc);
        AssertLogRelReturn(pDXShader->shaderInfo.cPatchConstantSignature <= 32, VERR_INVALID_STATE);
        if (pDXShader->shaderInfo.cPatchConstantSignature)
            pHlp->pfnSSMGetMem(pSSM, pDXShader->shaderInfo.aPatchConstantSignature, pDXShader->shaderInfo.cPatchConstantSignature * sizeof(SVGA3dDXSignatureEntry));

        rc = pHlp->pfnSSMGetU32(pSSM, &pDXShader->shaderInfo.cDclResource);
        AssertLogRelRCReturn(rc, rc);
        AssertLogRelReturn(pDXShader->shaderInfo.cDclResource <= SVGA3D_DX_MAX_SRVIEWS, VERR_INVALID_STATE);
        if (pDXShader->shaderInfo.cDclResource)
            pHlp->pfnSSMGetMem(pSSM, pDXShader->shaderInfo.aOffDclResource, pDXShader->shaderInfo.cDclResource * sizeof(uint32_t));

        DXShaderGenerateSemantics(&pDXShader->shaderInfo);
    }

    rc = pHlp->pfnSSMGetU32(pSSM, &pDXContext->pBackendDXContext->cSOTarget);
    AssertLogRelRCReturn(rc, rc);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackDXSaveState(PVGASTATECC pThisCC, PVMSVGA3DDXCONTEXT pDXContext, PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM)
{
    RT_NOREF(pThisCC);
    int rc;

    pHlp->pfnSSMPutU32(pSSM, pDXContext->pBackendDXContext->cShader);
    for (uint32_t i = 0; i < pDXContext->pBackendDXContext->cShader; ++i)
    {
        DXSHADER *pDXShader = &pDXContext->pBackendDXContext->paShader[i];

        pHlp->pfnSSMPutU32(pSSM, (uint32_t)pDXShader->enmShaderType);
        if (pDXShader->enmShaderType == SVGA3D_SHADERTYPE_INVALID)
            continue;

        pHlp->pfnSSMPutU32(pSSM, pDXShader->soid);

        pHlp->pfnSSMPutU32(pSSM, (uint32_t)pDXShader->shaderInfo.enmProgramType);

        pHlp->pfnSSMPutU32(pSSM, pDXShader->shaderInfo.cbBytecode);
        if (pDXShader->shaderInfo.cbBytecode)
            pHlp->pfnSSMPutMem(pSSM, pDXShader->shaderInfo.pvBytecode, pDXShader->shaderInfo.cbBytecode);

        pHlp->pfnSSMPutU32(pSSM, pDXShader->shaderInfo.cInputSignature);
        if (pDXShader->shaderInfo.cInputSignature)
            pHlp->pfnSSMPutMem(pSSM, pDXShader->shaderInfo.aInputSignature, pDXShader->shaderInfo.cInputSignature * sizeof(SVGA3dDXSignatureEntry));

        pHlp->pfnSSMPutU32(pSSM, pDXShader->shaderInfo.cOutputSignature);
        if (pDXShader->shaderInfo.cOutputSignature)
            pHlp->pfnSSMPutMem(pSSM, pDXShader->shaderInfo.aOutputSignature, pDXShader->shaderInfo.cOutputSignature * sizeof(SVGA3dDXSignatureEntry));

        pHlp->pfnSSMPutU32(pSSM, pDXShader->shaderInfo.cPatchConstantSignature);
        if (pDXShader->shaderInfo.cPatchConstantSignature)
            pHlp->pfnSSMPutMem(pSSM, pDXShader->shaderInfo.aPatchConstantSignature, pDXShader->shaderInfo.cPatchConstantSignature * sizeof(SVGA3dDXSignatureEntry));

        pHlp->pfnSSMPutU32(pSSM, pDXShader->shaderInfo.cDclResource);
        if (pDXShader->shaderInfo.cDclResource)
            pHlp->pfnSSMPutMem(pSSM, pDXShader->shaderInfo.aOffDclResource, pDXShader->shaderInfo.cDclResource * sizeof(uint32_t));
    }
    rc = pHlp->pfnSSMPutU32(pSSM, pDXContext->pBackendDXContext->cSOTarget);
    AssertLogRelRCReturn(rc, rc);

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) vmsvga3dBackQueryInterface(PVGASTATECC pThisCC, char const *pszInterfaceName, void *pvInterfaceFuncs, size_t cbInterfaceFuncs)
{
    RT_NOREF(pThisCC);

    int rc = VINF_SUCCESS;
    if (RTStrCmp(pszInterfaceName, VMSVGA3D_BACKEND_INTERFACE_NAME_DX) == 0)
    {
        if (cbInterfaceFuncs == sizeof(VMSVGA3DBACKENDFUNCSDX))
        {
            if (pvInterfaceFuncs)
            {
                VMSVGA3DBACKENDFUNCSDX *p = (VMSVGA3DBACKENDFUNCSDX *)pvInterfaceFuncs;
                p->pfnDXSaveState                 = vmsvga3dBackDXSaveState;
                p->pfnDXLoadState                 = vmsvga3dBackDXLoadState;
                p->pfnDXDefineContext             = vmsvga3dBackDXDefineContext;
                p->pfnDXDestroyContext            = vmsvga3dBackDXDestroyContext;
                p->pfnDXBindContext               = vmsvga3dBackDXBindContext;
                p->pfnDXSwitchContext             = vmsvga3dBackDXSwitchContext;
                p->pfnDXReadbackContext           = vmsvga3dBackDXReadbackContext;
                p->pfnDXInvalidateContext         = vmsvga3dBackDXInvalidateContext;
                p->pfnDXSetSingleConstantBuffer   = vmsvga3dBackDXSetSingleConstantBuffer;
                p->pfnDXSetShaderResources        = vmsvga3dBackDXSetShaderResources;
                p->pfnDXSetShader                 = vmsvga3dBackDXSetShader;
                p->pfnDXSetSamplers               = vmsvga3dBackDXSetSamplers;
                p->pfnDXDraw                      = vmsvga3dBackDXDraw;
                p->pfnDXDrawIndexed               = vmsvga3dBackDXDrawIndexed;
                p->pfnDXDrawInstanced             = vmsvga3dBackDXDrawInstanced;
                p->pfnDXDrawIndexedInstanced      = vmsvga3dBackDXDrawIndexedInstanced;
                p->pfnDXDrawAuto                  = vmsvga3dBackDXDrawAuto;
                p->pfnDXSetInputLayout            = vmsvga3dBackDXSetInputLayout;
                p->pfnDXSetVertexBuffers          = vmsvga3dBackDXSetVertexBuffers;
                p->pfnDXSetIndexBuffer            = vmsvga3dBackDXSetIndexBuffer;
                p->pfnDXSetTopology               = vmsvga3dBackDXSetTopology;
                p->pfnDXSetRenderTargets          = vmsvga3dBackDXSetRenderTargets;
                p->pfnDXSetBlendState             = vmsvga3dBackDXSetBlendState;
                p->pfnDXSetDepthStencilState      = vmsvga3dBackDXSetDepthStencilState;
                p->pfnDXSetRasterizerState        = vmsvga3dBackDXSetRasterizerState;
                p->pfnDXDefineQuery               = vmsvga3dBackDXDefineQuery;
                p->pfnDXDestroyQuery              = vmsvga3dBackDXDestroyQuery;
                p->pfnDXBeginQuery                = vmsvga3dBackDXBeginQuery;
                p->pfnDXEndQuery                  = vmsvga3dBackDXEndQuery;
                p->pfnDXSetPredication            = vmsvga3dBackDXSetPredication;
                p->pfnDXSetSOTargets              = vmsvga3dBackDXSetSOTargets;
                p->pfnDXSetViewports              = vmsvga3dBackDXSetViewports;
                p->pfnDXSetScissorRects           = vmsvga3dBackDXSetScissorRects;
                p->pfnDXClearRenderTargetView     = vmsvga3dBackDXClearRenderTargetView;
                p->pfnDXClearDepthStencilView     = vmsvga3dBackDXClearDepthStencilView;
                p->pfnDXPredCopyRegion            = vmsvga3dBackDXPredCopyRegion;
                p->pfnDXPredCopy                  = vmsvga3dBackDXPredCopy;
                p->pfnDXPresentBlt                = vmsvga3dBackDXPresentBlt;
                p->pfnDXGenMips                   = vmsvga3dBackDXGenMips;
                p->pfnDXDefineShaderResourceView  = vmsvga3dBackDXDefineShaderResourceView;
                p->pfnDXDestroyShaderResourceView = vmsvga3dBackDXDestroyShaderResourceView;
                p->pfnDXDefineRenderTargetView    = vmsvga3dBackDXDefineRenderTargetView;
                p->pfnDXDestroyRenderTargetView   = vmsvga3dBackDXDestroyRenderTargetView;
                p->pfnDXDefineDepthStencilView    = vmsvga3dBackDXDefineDepthStencilView;
                p->pfnDXDestroyDepthStencilView   = vmsvga3dBackDXDestroyDepthStencilView;
                p->pfnDXDefineElementLayout       = vmsvga3dBackDXDefineElementLayout;
                p->pfnDXDestroyElementLayout      = vmsvga3dBackDXDestroyElementLayout;
                p->pfnDXDefineBlendState          = vmsvga3dBackDXDefineBlendState;
                p->pfnDXDestroyBlendState         = vmsvga3dBackDXDestroyBlendState;
                p->pfnDXDefineDepthStencilState   = vmsvga3dBackDXDefineDepthStencilState;
                p->pfnDXDestroyDepthStencilState  = vmsvga3dBackDXDestroyDepthStencilState;
                p->pfnDXDefineRasterizerState     = vmsvga3dBackDXDefineRasterizerState;
                p->pfnDXDestroyRasterizerState    = vmsvga3dBackDXDestroyRasterizerState;
                p->pfnDXDefineSamplerState        = vmsvga3dBackDXDefineSamplerState;
                p->pfnDXDestroySamplerState       = vmsvga3dBackDXDestroySamplerState;
                p->pfnDXDefineShader              = vmsvga3dBackDXDefineShader;
                p->pfnDXDestroyShader             = vmsvga3dBackDXDestroyShader;
                p->pfnDXBindShader                = vmsvga3dBackDXBindShader;
                p->pfnDXDefineStreamOutput        = vmsvga3dBackDXDefineStreamOutput;
                p->pfnDXDestroyStreamOutput       = vmsvga3dBackDXDestroyStreamOutput;
                p->pfnDXSetStreamOutput           = vmsvga3dBackDXSetStreamOutput;
                p->pfnDXSetCOTable                = vmsvga3dBackDXSetCOTable;
                p->pfnDXBufferCopy                = vmsvga3dBackDXBufferCopy;
                p->pfnDXSurfaceCopyAndReadback    = vmsvga3dBackDXSurfaceCopyAndReadback;
                p->pfnDXMoveQuery                 = vmsvga3dBackDXMoveQuery;
                p->pfnDXBindAllShader             = vmsvga3dBackDXBindAllShader;
                p->pfnDXHint                      = vmsvga3dBackDXHint;
                p->pfnDXBufferUpdate              = vmsvga3dBackDXBufferUpdate;
                p->pfnDXCondBindAllShader         = vmsvga3dBackDXCondBindAllShader;
                p->pfnScreenCopy                  = vmsvga3dBackScreenCopy;
                p->pfnIntraSurfaceCopy            = vmsvga3dBackIntraSurfaceCopy;
                p->pfnDXResolveCopy               = vmsvga3dBackDXResolveCopy;
                p->pfnDXPredResolveCopy           = vmsvga3dBackDXPredResolveCopy;
                p->pfnDXPredConvertRegion         = vmsvga3dBackDXPredConvertRegion;
                p->pfnDXPredConvert               = vmsvga3dBackDXPredConvert;
                p->pfnWholeSurfaceCopy            = vmsvga3dBackWholeSurfaceCopy;
                p->pfnDXDefineUAView              = vmsvga3dBackDXDefineUAView;
                p->pfnDXDestroyUAView             = vmsvga3dBackDXDestroyUAView;
                p->pfnDXClearUAViewUint           = vmsvga3dBackDXClearUAViewUint;
                p->pfnDXClearUAViewFloat          = vmsvga3dBackDXClearUAViewFloat;
                p->pfnDXCopyStructureCount        = vmsvga3dBackDXCopyStructureCount;
                p->pfnDXSetUAViews                = vmsvga3dBackDXSetUAViews;
                p->pfnDXDrawIndexedInstancedIndirect = vmsvga3dBackDXDrawIndexedInstancedIndirect;
                p->pfnDXDrawInstancedIndirect     = vmsvga3dBackDXDrawInstancedIndirect;
                p->pfnDXDispatch                  = vmsvga3dBackDXDispatch;
                p->pfnDXDispatchIndirect          = vmsvga3dBackDXDispatchIndirect;
                p->pfnWriteZeroSurface            = vmsvga3dBackWriteZeroSurface;
                p->pfnHintZeroSurface             = vmsvga3dBackHintZeroSurface;
                p->pfnDXTransferToBuffer          = vmsvga3dBackDXTransferToBuffer;
                p->pfnLogicOpsBitBlt              = vmsvga3dBackLogicOpsBitBlt;
                p->pfnLogicOpsTransBlt            = vmsvga3dBackLogicOpsTransBlt;
                p->pfnLogicOpsStretchBlt          = vmsvga3dBackLogicOpsStretchBlt;
                p->pfnLogicOpsColorFill           = vmsvga3dBackLogicOpsColorFill;
                p->pfnLogicOpsAlphaBlend          = vmsvga3dBackLogicOpsAlphaBlend;
                p->pfnLogicOpsClearTypeBlend      = vmsvga3dBackLogicOpsClearTypeBlend;
                p->pfnDXSetCSUAViews              = vmsvga3dBackDXSetCSUAViews;
                p->pfnDXSetMinLOD                 = vmsvga3dBackDXSetMinLOD;
                p->pfnDXSetShaderIface            = vmsvga3dBackDXSetShaderIface;
                p->pfnSurfaceStretchBltNonMSToMS  = vmsvga3dBackSurfaceStretchBltNonMSToMS;
                p->pfnDXBindShaderIface           = vmsvga3dBackDXBindShaderIface;
                p->pfnVBDXClearRenderTargetViewRegion = vmsvga3dBackVBDXClearRenderTargetViewRegion;
                p->pfnVBDXClearUAV                = vmsvga3dBackVBDXClearUAV;
            }
        }
        else
        {
            AssertFailed();
            rc = VERR_INVALID_PARAMETER;
        }
    }
    else if (RTStrCmp(pszInterfaceName, VMSVGA3D_BACKEND_INTERFACE_NAME_DXVIDEO) == 0)
    {
        if (cbInterfaceFuncs == sizeof(VMSVGA3DBACKENDFUNCSDXVIDEO))
        {
            if (pvInterfaceFuncs)
            {
                VMSVGA3DBACKENDFUNCSDXVIDEO *p = (VMSVGA3DBACKENDFUNCSDXVIDEO *)pvInterfaceFuncs;
                p->pfnVBDXDefineVideoProcessor              = vmsvga3dBackVBDXDefineVideoProcessor;
                p->pfnVBDXDefineVideoDecoderOutputView      = vmsvga3dBackVBDXDefineVideoDecoderOutputView;
                p->pfnVBDXDefineVideoDecoder                = vmsvga3dBackVBDXDefineVideoDecoder;
                p->pfnVBDXVideoDecoderBeginFrame            = vmsvga3dBackVBDXVideoDecoderBeginFrame;
                p->pfnVBDXVideoDecoderSubmitBuffers         = vmsvga3dBackVBDXVideoDecoderSubmitBuffers;
                p->pfnVBDXVideoDecoderEndFrame              = vmsvga3dBackVBDXVideoDecoderEndFrame;
                p->pfnVBDXDefineVideoProcessorInputView     = vmsvga3dBackVBDXDefineVideoProcessorInputView;
                p->pfnVBDXDefineVideoProcessorOutputView    = vmsvga3dBackVBDXDefineVideoProcessorOutputView;
                p->pfnVBDXVideoProcessorBlt                 = vmsvga3dBackVBDXVideoProcessorBlt;
                p->pfnVBDXDestroyVideoDecoder               = vmsvga3dBackVBDXDestroyVideoDecoder;
                p->pfnVBDXDestroyVideoDecoderOutputView     = vmsvga3dBackVBDXDestroyVideoDecoderOutputView;
                p->pfnVBDXDestroyVideoProcessor             = vmsvga3dBackVBDXDestroyVideoProcessor;
                p->pfnVBDXDestroyVideoProcessorInputView    = vmsvga3dBackVBDXDestroyVideoProcessorInputView;
                p->pfnVBDXDestroyVideoProcessorOutputView   = vmsvga3dBackVBDXDestroyVideoProcessorOutputView;
                p->pfnVBDXVideoProcessorSetOutputTargetRect = vmsvga3dBackVBDXVideoProcessorSetOutputTargetRect;
                p->pfnVBDXVideoProcessorSetOutputBackgroundColor = vmsvga3dBackVBDXVideoProcessorSetOutputBackgroundColor;
                p->pfnVBDXVideoProcessorSetOutputColorSpace = vmsvga3dBackVBDXVideoProcessorSetOutputColorSpace;
                p->pfnVBDXVideoProcessorSetOutputAlphaFillMode = vmsvga3dBackVBDXVideoProcessorSetOutputAlphaFillMode;
                p->pfnVBDXVideoProcessorSetOutputConstriction = vmsvga3dBackVBDXVideoProcessorSetOutputConstriction;
                p->pfnVBDXVideoProcessorSetOutputStereoMode = vmsvga3dBackVBDXVideoProcessorSetOutputStereoMode;
                p->pfnVBDXVideoProcessorSetStreamFrameFormat = vmsvga3dBackVBDXVideoProcessorSetStreamFrameFormat;
                p->pfnVBDXVideoProcessorSetStreamColorSpace = vmsvga3dBackVBDXVideoProcessorSetStreamColorSpace;
                p->pfnVBDXVideoProcessorSetStreamOutputRate = vmsvga3dBackVBDXVideoProcessorSetStreamOutputRate;
                p->pfnVBDXVideoProcessorSetStreamSourceRect = vmsvga3dBackVBDXVideoProcessorSetStreamSourceRect;
                p->pfnVBDXVideoProcessorSetStreamDestRect   = vmsvga3dBackVBDXVideoProcessorSetStreamDestRect;
                p->pfnVBDXVideoProcessorSetStreamAlpha      = vmsvga3dBackVBDXVideoProcessorSetStreamAlpha;
                p->pfnVBDXVideoProcessorSetStreamPalette    = vmsvga3dBackVBDXVideoProcessorSetStreamPalette;
                p->pfnVBDXVideoProcessorSetStreamPixelAspectRatio = vmsvga3dBackVBDXVideoProcessorSetStreamPixelAspectRatio;
                p->pfnVBDXVideoProcessorSetStreamLumaKey    = vmsvga3dBackVBDXVideoProcessorSetStreamLumaKey;
                p->pfnVBDXVideoProcessorSetStreamStereoFormat = vmsvga3dBackVBDXVideoProcessorSetStreamStereoFormat;
                p->pfnVBDXVideoProcessorSetStreamAutoProcessingMode = vmsvga3dBackVBDXVideoProcessorSetStreamAutoProcessingMode;
                p->pfnVBDXVideoProcessorSetStreamFilter     = vmsvga3dBackVBDXVideoProcessorSetStreamFilter;
                p->pfnVBDXVideoProcessorSetStreamRotation   = vmsvga3dBackVBDXVideoProcessorSetStreamRotation;
                p->pfnVBDXGetVideoCapability                = vmsvga3dBackVBDXGetVideoCapability;
                p->pfnVBDXClearVDOV                         = vmsvga3dBackVBDXClearVDOV;
                p->pfnVBDXClearVPIV                         = vmsvga3dBackVBDXClearVPIV;
                p->pfnVBDXClearVPOV                         = vmsvga3dBackVBDXClearVPOV;
            }
        }
        else
        {
            AssertFailed();
            rc = VERR_INVALID_PARAMETER;
        }
    }
    else if (RTStrCmp(pszInterfaceName, VMSVGA3D_BACKEND_INTERFACE_NAME_MAP) == 0)
    {
        if (cbInterfaceFuncs == sizeof(VMSVGA3DBACKENDFUNCSMAP))
        {
            if (pvInterfaceFuncs)
            {
                VMSVGA3DBACKENDFUNCSMAP *p = (VMSVGA3DBACKENDFUNCSMAP *)pvInterfaceFuncs;
                p->pfnSurfaceMap   = vmsvga3dBackSurfaceMap;
                p->pfnSurfaceUnmap = vmsvga3dBackSurfaceUnmap;
            }
        }
        else
        {
            AssertFailed();
            rc = VERR_INVALID_PARAMETER;
        }
    }
    else if (RTStrCmp(pszInterfaceName, VMSVGA3D_BACKEND_INTERFACE_NAME_GBO) == 0)
    {
        if (cbInterfaceFuncs == sizeof(VMSVGA3DBACKENDFUNCSGBO))
        {
            if (pvInterfaceFuncs)
            {
                VMSVGA3DBACKENDFUNCSGBO *p = (VMSVGA3DBACKENDFUNCSGBO *)pvInterfaceFuncs;
                p->pfnScreenTargetBind   = vmsvga3dScreenTargetBind;
                p->pfnScreenTargetUpdate = vmsvga3dScreenTargetUpdate;
            }
        }
        else
        {
            AssertFailed();
            rc = VERR_INVALID_PARAMETER;
        }
    }
    else if (RTStrCmp(pszInterfaceName, VMSVGA3D_BACKEND_INTERFACE_NAME_3D) == 0)
    {
        if (cbInterfaceFuncs == sizeof(VMSVGA3DBACKENDFUNCS3D))
        {
            if (pvInterfaceFuncs)
            {
                VMSVGA3DBACKENDFUNCS3D *p = (VMSVGA3DBACKENDFUNCS3D *)pvInterfaceFuncs;
                p->pfnInit                     = vmsvga3dBackInit;
                p->pfnPowerOn                  = vmsvga3dBackPowerOn;
                p->pfnTerminate                = vmsvga3dBackTerminate;
                p->pfnReset                    = vmsvga3dBackReset;
                p->pfnQueryCaps                = vmsvga3dBackQueryCaps;
                p->pfnChangeMode               = vmsvga3dBackChangeMode;
                p->pfnCreateTexture            = vmsvga3dBackCreateTexture;
                p->pfnSurfaceDestroy           = vmsvga3dBackSurfaceDestroy;
                p->pfnSurfaceInvalidateImage   = vmsvga3dBackSurfaceInvalidateImage;
                p->pfnSurfaceCopy              = vmsvga3dBackSurfaceCopy;
                p->pfnSurfaceDMACopyBox        = vmsvga3dBackSurfaceDMACopyBox;
                p->pfnSurfaceStretchBlt        = vmsvga3dBackSurfaceStretchBlt;
                p->pfnUpdateHostScreenViewport = vmsvga3dBackUpdateHostScreenViewport;
                p->pfnDefineScreen             = vmsvga3dBackDefineScreen;
                p->pfnDestroyScreen            = vmsvga3dBackDestroyScreen;
                p->pfnSurfaceBlitToScreen      = vmsvga3dBackSurfaceBlitToScreen;
                p->pfnSurfaceUpdateHeapBuffers = vmsvga3dBackSurfaceUpdateHeapBuffers;
            }
        }
        else
        {
            AssertFailed();
            rc = VERR_INVALID_PARAMETER;
        }
    }
    else if (RTStrCmp(pszInterfaceName, VMSVGA3D_BACKEND_INTERFACE_NAME_VGPU9) == 0)
    {
        if (cbInterfaceFuncs == sizeof(VMSVGA3DBACKENDFUNCSVGPU9))
        {
            if (pvInterfaceFuncs)
            {
                VMSVGA3DBACKENDFUNCSVGPU9 *p = (VMSVGA3DBACKENDFUNCSVGPU9 *)pvInterfaceFuncs;
                p->pfnContextDefine            = vmsvga3dBackContextDefine;
                p->pfnContextDestroy           = vmsvga3dBackContextDestroy;
                p->pfnSetTransform             = vmsvga3dBackSetTransform;
                p->pfnSetZRange                = vmsvga3dBackSetZRange;
                p->pfnSetRenderState           = vmsvga3dBackSetRenderState;
                p->pfnSetRenderTarget          = vmsvga3dBackSetRenderTarget;
                p->pfnSetTextureState          = vmsvga3dBackSetTextureState;
                p->pfnSetMaterial              = vmsvga3dBackSetMaterial;
                p->pfnSetLightData             = vmsvga3dBackSetLightData;
                p->pfnSetLightEnabled          = vmsvga3dBackSetLightEnabled;
                p->pfnSetViewPort              = vmsvga3dBackSetViewPort;
                p->pfnSetClipPlane             = vmsvga3dBackSetClipPlane;
                p->pfnCommandClear             = vmsvga3dBackCommandClear;
                p->pfnDrawPrimitives           = vmsvga3dBackDrawPrimitives;
                p->pfnSetScissorRect           = vmsvga3dBackSetScissorRect;
                p->pfnGenerateMipmaps          = vmsvga3dBackGenerateMipmaps;
                p->pfnShaderDefine             = vmsvga3dBackShaderDefine;
                p->pfnShaderDestroy            = vmsvga3dBackShaderDestroy;
                p->pfnShaderSet                = vmsvga3dBackShaderSet;
                p->pfnShaderSetConst           = vmsvga3dBackShaderSetConst;
                p->pfnOcclusionQueryCreate     = vmsvga3dBackOcclusionQueryCreate;
                p->pfnOcclusionQueryDelete     = vmsvga3dBackOcclusionQueryDelete;
                p->pfnOcclusionQueryBegin      = vmsvga3dBackOcclusionQueryBegin;
                p->pfnOcclusionQueryEnd        = vmsvga3dBackOcclusionQueryEnd;
                p->pfnOcclusionQueryGetData    = vmsvga3dBackOcclusionQueryGetData;
            }
        }
        else
        {
            AssertFailed();
            rc = VERR_INVALID_PARAMETER;
        }
    }
    else
        rc = VERR_NOT_IMPLEMENTED;
    return rc;
}


extern VMSVGA3DBACKENDDESC const g_BackendDX =
{
    "DX",
    vmsvga3dBackQueryInterface
};
