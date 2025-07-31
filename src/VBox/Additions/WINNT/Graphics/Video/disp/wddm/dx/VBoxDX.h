/* $Id: VBoxDX.h 110310 2025-07-18 15:27:55Z vitali.pelenjow@oracle.com $ */
/** @file
 * VBoxVideo Display D3D User mode dll
 */

/*
 * Copyright (C) 2011-2025 Oracle and/or its affiliates.
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_dx_VBoxDX_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_dx_VBoxDX_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/assert.h>
#include <iprt/handletable.h>

#include <VBoxMPIf.h>

#pragma pack(1) /* VMSVGA structures are '__packed'. */
#include <svga3d_caps.h>
#include <svga3d_reg.h>
#include <svga3d_shaderdefs.h>
#include <svga_escape.h>
#include <svga_overlay.h>
#include <vbsvga3d_dx.h>
#pragma pack()

#include <d3d11_1.h>

#define VBOXDX_SHADER_ALLOCATION_SIZE SVGA3D_MAX_SHADER_MEMORY_BYTES

#ifdef DEBUG_sunlover
#define DEBUG_BREAKPOINT_TEST() do { ASMBreakpoint(); } while (0)
#else
#define DEBUG_BREAKPOINT_TEST() do { } while (0)
#endif

/**@def FLOAT_FMT_STR
 * Format string bits to go with FLOAT_FMT_ARGS. */
#define FLOAT_FMT_STR                  "%s%u.%06u"
/** @def FLOAT_FMT_ARGS
 * Format arguments for a float value, corresponding to FLOAT_FMT_STR.
 * @param   r       The floating point value to format.  */
#define FLOAT_FMT_ARGS(r)              (r) >= 0.0f ? "" : "-", (unsigned)RT_ABS(r) \
                                       , (unsigned)(RT_ABS((r) - (float)(unsigned)(r)) * 1000000.0f)

typedef struct VBOXDXADAPTER
{
    HANDLE hRTAdapter;
    UINT uIfVersion;
    UINT uRtVersion;
    D3DDDI_ADAPTERCALLBACKS RtCallbacks;

    VBOXVIDEO_HWTYPE enmHwType;     /* VBOXVIDEO_HWTYPE_* */

    bool f3D;

    uint32_t fVBoxCaps;

    VBOXWDDM_QAI AdapterInfo;
} VBOXDXADAPTER, *PVBOXDXADAPTER;

typedef struct VBOXDX_BLENDSTATE
{
    D3D10DDI_HRTBLENDSTATE hRTBlendState;
    D3D11_1_DDI_BLEND_DESC BlendDesc;
    uint32_t uBlendId;
} VBOXDX_BLENDSTATE, *PVBOXDX_BLENDSTATE;

typedef struct VBOXDX_DEPTHSTENCIL_STATE
{
    D3D10DDI_HRTDEPTHSTENCILSTATE hRTDepthStencilState;
    D3D10_DDI_DEPTH_STENCIL_DESC DepthStencilDesc;
    uint32_t uDepthStencilId;
} VBOXDX_DEPTHSTENCIL_STATE, *PVBOXDX_DEPTHSTENCIL_STATE;

typedef struct VBOXDX_RASTERIZER_STATE
{
    D3D10DDI_HRTRASTERIZERSTATE hRTRasterizerState;
    D3D11_1_DDI_RASTERIZER_DESC RasterizerDesc;
    uint32_t uRasterizerId;
} VBOXDX_RASTERIZER_STATE, *PVBOXDX_RASTERIZER_STATE;

typedef struct VBOXDX_SAMPLER_STATE
{
    D3D10DDI_HRTSAMPLER hRTSampler;
    D3D10_DDI_SAMPLER_DESC SamplerDesc;
    uint32_t uSamplerId;
} VBOXDX_SAMPLER_STATE, *PVBOXDX_SAMPLER_STATE;

typedef struct VBOXDXELEMENTLAYOUT
{
    D3D10DDI_HRTELEMENTLAYOUT      hRTElementLayout;
    UINT                           NumElements;
    uint32_t                       uElementLayoutId;
    RT_FLEXIBLE_ARRAY_EXTENSION
    D3D10DDIARG_INPUT_ELEMENT_DESC aVertexElements[RT_FLEXIBLE_ARRAY];
} VBOXDXELEMENTLAYOUT, *PVBOXDXELEMENTLAYOUT;

/*
 * A Context Object Allocation (COA) holds up to VBOXDX_COALLOCATION_MAX_OBJECTS objects (queries, etc)
 */
#define VBOXDX_COALLOCATION_MAX_OBJECTS (64)                /* Use a 64 bit field as free bitmap. */

/*
 * This structure is allocated by the driver. It is used for accounting of allocations
 * and for storing information which might be needed after the resource is destroyed by D3D runtime.
 * KM stands for Kernel Mode.
 */
typedef struct VBOXDXKMRESOURCE
{
    RTLISTNODE                     nodeResource;            /* VBOXDX_DEVICE::listResources, listDestroyedResources. */
    D3DKMT_HANDLE                  hAllocation;
    struct
    {
        uint32_t                   fOpened : 1;
        uint32_t                   fReserved : 31;
    } flags;
    VBOXDXALLOCATIONDESC           AllocationDesc;
    union
    {
        struct /* D3D resource */
        {
            struct VBOXDX_RESOURCE *pResource;              /* The structure allocated by D3D runtime. */
            RTLISTNODE             nodeStaging;             /* VBOXDX_DEVICE::listStagingResources if this resource is a staging buffer. */
        } resource;
        struct /* Context object allocation */
        {
            RTLISTNODE             nodeAllocationsChain;    /* CO allocations can be chained. */
            uint64_t               u64Bitmap;               /* Bitmap of allocated blocks. */
            uint32_t               aOffset[VBOXDX_COALLOCATION_MAX_OBJECTS]; /* Start offsets of blocks. */
        } co;
        struct /* Shaders allocation */
        {
            RTLISTNODE             nodeAllocationsChain;    /* Shaders allocations are chained (VBOXDX_DEVICE::listShadersAllocations). */
        } shaders;
    };
} VBOXDXKMRESOURCE, *PVBOXDXKMRESOURCE;

typedef struct VBOXDXSHADER
{
    RTLISTNODE                  node;                       /* VBOXDX_DEVICE::listShaders */
    D3D10DDI_HRTSHADER          hRTShader;
    uint32_t                    uShaderId;
    SVGA3dShaderType            enmShaderType;
    uint32_t                    cbShader;                   /* Size of the shader code in bytes. */
    uint32_t                    cbSignatures;               /* Size of the shader signatures, which follow the shader bytecode. */
    uint32_t                    offShader;                  /* Offset of the shader in the shaders allocation. */
    uint32_t                   *pu32Bytecode;               /* Bytecode follows the VBOXDXSHADER structure. */
    SVGA3dDXSignatureHeader    *pSignatures;                /* Signatures follow the bytecode. */
    PVBOXDXKMRESOURCE           pShadersAllocation;         /* Where the bytecode and signatures are placed to. */
    struct
    {
        UINT                    NumEntries;
        UINT                    NumStrides;
        UINT                    BufferStridesInBytes[SVGA3D_DX_MAX_SOTARGETS];
        UINT                    RasterizedStream;
        uint32_t                uStreamOutputId;            /* Id of the stream output. */
        uint32_t                offStreamOutputDecls;       /* Offset of the declarations in the CO allocation. */
        uint32_t                cbOutputStreamDecls;
        PVBOXDXKMRESOURCE       pCOAllocation;
    } gs;                                                   /* For GS. */
    /* shader bytecode */
    /* shader signatures */
} VBOXDXSHADER, *PVBOXDXSHADER;

typedef enum VBOXDXQUERYSTATE
{
    VBOXDXQUERYSTATE_CREATED = 0,
    VBOXDXQUERYSTATE_BUILDING,
    VBOXDXQUERYSTATE_ISSUED,
    VBOXDXQUERYSTATE_SIGNALED
} VBOXDXQUERYSTATE;

/*
 * Queries are stored in allocations, each of which holds an array of queries of the same type.
 */
typedef struct VBOXDXQUERY
{
    RTLISTNODE                  nodeQuery;                  /* VBOXDX_DEVICE::listQueries */
    D3D10DDI_HRTQUERY           hRTQuery;
    D3D10DDI_QUERY              Query;
    struct
    {
        SVGA3dQueryType         queryType;
        SVGA3dDXQueryFlags      flags;
    } svga;
    VBOXDXQUERYSTATE            enmQueryState;
    uint32_t                    uQueryId;
    uint32_t                    offQuery;                   /* Offset of the query in the query allocation. */
    PVBOXDXKMRESOURCE           pCOAllocation;
    uint64_t u64Value;
    /* Result for queries in SIGNALED state. */
} VBOXDXQUERY, *PVBOXDXQUERY;

typedef struct VBOXDX_RESOURCE
{
    PVBOXDXKMRESOURCE              pKMResource;
    D3D10DDI_HRTRESOURCE           hRTResource;
    D3D10DDIRESOURCE_TYPE          ResourceDimension;
    D3D10_DDI_RESOURCE_USAGE       Usage;
    UINT                           cSubresources;
    union
    {
        D3D10_DDI_MAP              DDIMap;
        UINT                       uMap;
    };

    RTLISTANCHOR                   listSRV;                 /* Shader resource views created for this resource. */
    RTLISTANCHOR                   listRTV;                 /* Render target views. */
    RTLISTANCHOR                   listDSV;                 /* Depth stencil views. */
    RTLISTANCHOR                   listUAV;                 /* Unordered access views. */
    RTLISTANCHOR                   listVDOV;                /* Video decoder output views. */
    RTLISTANCHOR                   listVPIV;                /* Video processor input views. */
    RTLISTANCHOR                   listVPOV;                /* Video processor output views. */

    RT_FLEXIBLE_ARRAY_EXTENSION
    D3D10DDI_MIPINFO               aMipInfoList[RT_FLEXIBLE_ARRAY]; /** @todo Remove this array if it will not be needed. */
} VBOXDX_RESOURCE, *PVBOXDX_RESOURCE;

typedef struct VBOXDXSHADERRESOURCEVIEW
{
    RTLISTNODE                     nodeView;                /* listSRV of the resource. */
    bool                           fDefined : 1;            /* Whether the view has been defined on the host. */
    D3D10DDI_HRTSHADERRESOURCEVIEW hRTShaderResourceView;
    uint32_t                       uShaderResourceViewId;
    PVBOXDX_RESOURCE               pResource;
    DXGI_FORMAT                    Format;
    D3D10DDIRESOURCE_TYPE          ResourceDimension;
    union
    {
        D3D10DDIARG_BUFFER_SHADERRESOURCEVIEW    Buffer;
        D3D10DDIARG_TEX1D_SHADERRESOURCEVIEW     Tex1D;
        D3D10DDIARG_TEX2D_SHADERRESOURCEVIEW     Tex2D;
        D3D10DDIARG_TEX3D_SHADERRESOURCEVIEW     Tex3D;
        D3D10_1DDIARG_TEXCUBE_SHADERRESOURCEVIEW TexCube;
        D3D11DDIARG_BUFFEREX_SHADERRESOURCEVIEW  BufferEx;
    } DimensionDesc;
    struct
    {
        SVGA3dSurfaceFormat        format;
        SVGA3dResourceType         resourceDimension;
        SVGA3dShaderResourceViewDesc desc;
    } svga;
} VBOXDXSHADERRESOURCEVIEW, *PVBOXDXSHADERRESOURCEVIEW;

typedef struct VBOXDXRENDERTARGETVIEW
{
    RTLISTNODE                     nodeView;                /* listRTV of the resource. */
    bool                           fDefined : 1;            /* Whether the view has been defined on the host. */
    D3D10DDI_HRTRENDERTARGETVIEW   hRTRenderTargetView;
    uint32_t                       uRenderTargetViewId;
    PVBOXDX_RESOURCE               pResource;
    DXGI_FORMAT                    Format;
    D3D10DDIRESOURCE_TYPE          ResourceDimension;
    union
    {
        D3D10DDIARG_BUFFER_RENDERTARGETVIEW  Buffer;
        D3D10DDIARG_TEX1D_RENDERTARGETVIEW   Tex1D;
        D3D10DDIARG_TEX2D_RENDERTARGETVIEW   Tex2D;
        D3D10DDIARG_TEX3D_RENDERTARGETVIEW   Tex3D;
        D3D10DDIARG_TEXCUBE_RENDERTARGETVIEW TexCube;
    } DimensionDesc;
    struct
    {
        SVGA3dSurfaceFormat        format;
        SVGA3dResourceType         resourceDimension;
        SVGA3dRenderTargetViewDesc desc;
    } svga;
} VBOXDXRENDERTARGETVIEW, *PVBOXDXRENDERTARGETVIEW;

typedef struct VBOXDXDEPTHSTENCILVIEW
{
    RTLISTNODE                     nodeView;                /* listDSV of the resource. */
    bool                           fDefined : 1;            /* Whether the view has been defined on the host. */
    D3D10DDI_HRTDEPTHSTENCILVIEW   hRTDepthStencilView;
    uint32_t                       uDepthStencilViewId;
    PVBOXDX_RESOURCE               pResource;
    DXGI_FORMAT                    Format;
    D3D10DDIRESOURCE_TYPE          ResourceDimension;
    UINT                           Flags;
    union
    {
        D3D10DDIARG_TEX1D_DEPTHSTENCILVIEW   Tex1D;
        D3D10DDIARG_TEX2D_DEPTHSTENCILVIEW   Tex2D;
        D3D10DDIARG_TEXCUBE_DEPTHSTENCILVIEW TexCube;
    } DimensionDesc;
    struct
    {
        SVGA3dSurfaceFormat        format;
        SVGA3dResourceType         resourceDimension;
        uint32_t                   mipSlice;
        uint32_t                   firstArraySlice;
        uint32_t                   arraySize;
        SVGA3DCreateDSViewFlags    flags;
    } svga;
} VBOXDXDEPTHSTENCILVIEW, *PVBOXDXDEPTHSTENCILVIEW;


typedef struct VBOXDXUNORDEREDACCESSVIEW
{
    RTLISTNODE                     nodeView;                /* listUAV of the resource. */
    bool                           fDefined : 1;            /* Whether the view has been defined on the host. */
    D3D11DDI_HRTUNORDEREDACCESSVIEW hRTUnorderedAccessView;
    uint32_t                       uUnorderedAccessViewId;
    PVBOXDX_RESOURCE               pResource;
    DXGI_FORMAT                    Format;
    D3D10DDIRESOURCE_TYPE          ResourceDimension;
    union
    {
        D3D11DDIARG_BUFFER_UNORDEREDACCESSVIEW    Buffer;
        D3D11DDIARG_TEX1D_UNORDEREDACCESSVIEW     Tex1D;
        D3D11DDIARG_TEX2D_UNORDEREDACCESSVIEW     Tex2D;
        D3D11DDIARG_TEX3D_UNORDEREDACCESSVIEW     Tex3D;
    } DimensionDesc;
    struct
    {
        SVGA3dSurfaceFormat        format;
        SVGA3dResourceType         resourceDimension;
        SVGA3dUAViewDesc           desc;
    } svga;
} VBOXDXUNORDEREDACCESSVIEW, *PVBOXDXUNORDEREDACCESSVIEW;


typedef struct VBOXDXVIDEOPROCESSORINPUTVIEW
{
    RTLISTNODE                     nodeView;                /* listVPIV of the resource. */
    bool                           fDefined : 1;            /* Whether the view has been defined on the host. */
    D3D11_1DDI_HRTVIDEOPROCESSORINPUTVIEW hRTView;
    uint32_t                       uVideoProcessorInputViewId;
    PVBOXDX_RESOURCE               pResource;

    struct
    {
        VBSVGA3dVideoProcessorDesc ContentDesc;
        VBSVGA3dVPIVDesc           VPIVDesc;
    } svga;
} VBOXDXVIDEOPROCESSORINPUTVIEW, *PVBOXDXVIDEOPROCESSORINPUTVIEW;


typedef struct VBOXDXVIDEOPROCESSOROUTPUTVIEW
{
    RTLISTNODE                     nodeView;                /* listVPOV of the resource. */
    bool                           fDefined : 1;            /* Whether the view has been defined on the host. */
    D3D11_1DDI_HRTVIDEOPROCESSOROUTPUTVIEW hRTView;
    uint32_t                       uVideoProcessorOutputViewId;
    PVBOXDX_RESOURCE               pResource;

    struct
    {
        VBSVGA3dVideoProcessorDesc ContentDesc;
        VBSVGA3dVPOVDesc           VPOVDesc;
    } svga;
} VBOXDXVIDEOPROCESSOROUTPUTVIEW, *PVBOXDXVIDEOPROCESSOROUTPUTVIEW;


typedef struct VBOXDXVIDEOPROCESSORFILTER
{
    BOOL Enable;
    int Level;
} VBOXDXVIDEOPROCESSORFILTER;


typedef struct VBOXDXVIDEOPROCESSORSTREAM
{
    D3D11_1DDI_VIDEO_FRAME_FORMAT FrameFormat;
    D3D11_1DDI_VIDEO_PROCESSOR_COLOR_SPACE ColorSpace;
    struct {
        D3D11_1DDI_VIDEO_PROCESSOR_OUTPUT_RATE OutputRate;
        BOOL RepeatFrame;
        DXGI_RATIONAL CustomRate;
    } OutputRate;
    struct {
        BOOL Enable;
        RECT SourceRect;
    } SourceRect;
    struct {
        BOOL Enable;
        RECT DestRect;
    } DestRect;
    struct {
        BOOL Enable;
        FLOAT Lower;
        FLOAT Upper;
    } LumaKey;
    struct {
        BOOL Enable;
    } AutoProcessingMode;
    struct {
        BOOL Enable;
        D3D11_1DDI_VIDEO_PROCESSOR_STEREO_FORMAT Format;
    } StereoFormat;
    struct {
        BOOL Enable;
        D3D11_1DDI_VIDEO_PROCESSOR_ROTATION Rotation;
    } Rotation;
    VBOXDXVIDEOPROCESSORFILTER aFilters[D3D11_1DDI_VIDEO_PROCESSOR_FILTER_STEREO_ADJUSTMENT + 1];
} VBOXDXVIDEOPROCESSORSTREAM;


typedef struct VBOXDXVIDEOPROCESSOR
{
    D3D11_1DDI_HRTVIDEOPROCESSOR   hRTVideoProcessor;
    uint32_t                       uVideoProcessorId;
    struct {
        VBSVGA3dVideoProcessorDesc desc;
    } svga;
    struct {
        BOOL Enable;
        RECT Rect;
    } OutputRect;
    struct {
        BOOL YCbCr;
        D3D11_1DDI_VIDEO_COLOR Color;
    } OutputBackgroundColor;
    D3D11_1DDI_VIDEO_PROCESSOR_COLOR_SPACE Colorspace;
    struct {
        BOOL Enabled;
        SIZE ConstrictonSize;
    } OutputConstriction;

    struct {
        D3D11_1DDI_VIDEO_PROCESSOR_ALPHA_FILL_MODE FillMode;
        UINT StreamIndex;
    } AlphaFillMode;

    struct {
        BOOL Enable;
    } StereoMode;

    VBOXDXVIDEOPROCESSORSTREAM aStreams[VBSVGA3D_MAX_VIDEO_STREAMS];
} VBOXDXVIDEOPROCESSOR, *PVBOXDXVIDEOPROCESSOR;


typedef struct VBOXDXVIDEOPROCESSORENUM
{
    D3D11_1DDI_HRTVIDEOPROCESSORENUM hRTVideoProcessorEnum;

    struct {
        VBSVGA3dVideoProcessorDesc desc;
    } svga;
} VBOXDXVIDEOPROCESSORENUM, *PVBOXDXVIDEOPROCESSORENUM;


typedef struct VBOXDXVIDEODECODEROUTPUTVIEW
{
    RTLISTNODE                     nodeView;                /* listVDOV of the resource. */
    bool                           fDefined : 1;            /* Whether the view has been defined on the host. */
    D3D11_1DDI_HRTVIDEODECODEROUTPUTVIEW hRTView;
    uint32_t                       uVideoDecoderOutputViewId;
    PVBOXDX_RESOURCE               pResource;

    struct {
        VBSVGA3dVDOVDesc           desc;
    } svga;
} VBOXDXVIDEODECODEROUTPUTVIEW, *PVBOXDXVIDEODECODEROUTPUTVIEW;


typedef struct VBOXDXVIDEODECODER
{
    D3D11_1DDI_HRTDECODE           hRTVideoDecoder;
    uint32_t                       uVideoDecoderId;

    struct {
        VBSVGA3dVideoDecoderDesc   Desc;
        VBSVGA3dVideoDecoderConfig Config;
    } svga;

    struct {
        PVBOXDXVIDEODECODEROUTPUTVIEW pOutputView;
    } Frame;
} VBOXDXVIDEODECODER, *PVBOXDXVIDEODECODER;


typedef struct VBOXDXVIDEOCRYPTOSESSION
{
    D3D11_1DDI_HRTCRYPTOSESSION hRTCryptoSession;
    uint32_t au32Dummy[3];
} VBOXDXVIDEOCRYPTOSESSION, *PVBOXDXVIDEOCRYPTOSESSION;


typedef struct VBOXDXVIDEOAUTHCHANNEL
{
    D3D11_1DDI_HRTAUTHCHANNEL hRTAuthChannel;
    uint32_t au32Dummy[4];
} VBOXDXVIDEOAUTHCHANNEL, *PVBOXDXVIDEOAUTHCHANNEL;


typedef struct VBOXDXCONSTANTBUFFERSSTATE
{
    UINT                           StartSlot;
    UINT                           NumBuffers;
    VBOXDX_RESOURCE               *apResource[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
    UINT                           aFirstConstant[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
    UINT                           aNumConstants[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
} VBOXDXCONSTANTBUFFERSSTATE, *PVBOXDXCONSTANTBUFFERSSTATE;


typedef struct VBOXDXVERTEXBUFFERSSTATE
{
    UINT                           StartSlot;
    UINT                           NumBuffers;
    VBOXDX_RESOURCE               *apResource[SVGA3D_MAX_VERTEX_ARRAYS];
    UINT                           aStrides[SVGA3D_MAX_VERTEX_ARRAYS];
    UINT                           aOffsets[SVGA3D_MAX_VERTEX_ARRAYS];
} VBOXDXVERTEXBUFFERSSTATE, *PVBOXDXVERTEXBUFFERSSTATE;


typedef struct VBOXDXINDEXBUFFERSTATE
{
    PVBOXDX_RESOURCE               pBuffer;
    DXGI_FORMAT                    Format;
    UINT                           Offset;
} VBOXDXINDEXBUFFERSTATE, *PVBOXDXINDEXBUFFERSTATE;


typedef struct VBOXDXSRVSTATE
{
    uint32_t cShaderResourceView;
    PVBOXDXSHADERRESOURCEVIEW apShaderResourceView[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
} VBOXDXSRVSTATE, *PVBOXDXSRVSTATE;


typedef struct VBOXDX_DEVICE
{
    /* DX runtime data. */
    D3D10DDI_HRTDEVICE             hRTDevice;                /* Handle to use when calling 'pRTCallbacks'. */
    UINT                           uDDIVersion;              /* Direct3D DDI version which this device provides. */
    UINT                           uCreateDeviceFlags;       /* Requested pipeline level and threading. */
    D3DDDI_DEVICECALLBACKS const  *pRTCallbacks;             /* Direct3D runtime functions that the driver can use to access kernel services. */
    DXGI_DDI_BASE_CALLBACKS const *pDXGIBaseCallbacks;       /* Direct3D runtime functions that the driver can use to access kernel services. */

    D3D10DDI_HRTCORELAYER          hRTCoreLayer;             /* Handle to use when calling 'pUMCallbacks'. */
    D3D11DDI_CORELAYER_DEVICECALLBACKS const *pUMCallbacks;  /* Direct3D 11 runtime functions which the driver can use to access core user-mode runtime functionality. */

    /* VBoxDX device data. */
    PVBOXDXADAPTER            pAdapter;                      /* Adapter which created this device. */

    /* Miniport context. */
    HANDLE                    hContext;
    VOID                      *pCommandBuffer;
    UINT                      CommandBufferSize;
    D3DDDI_ALLOCATIONLIST     *pAllocationList;
    UINT                      AllocationListSize;
    D3DDDI_PATCHLOCATIONLIST  *pPatchLocationList;
    UINT                      PatchLocationListSize;

    UINT                      cbCommandBuffer;               /* Size of commands in the buffer */
    UINT                      cbCommandReserved;             /* Size of the current command. */
    UINT                      cAllocations;                  /* Number of allocation in pAllocationList. */
    UINT                      cPatchLocations;               /* Number of patch locations. */

    /* Handle tables for various objects. */
    RTHANDLETABLE hHTBlendState;
    RTHANDLETABLE hHTDepthStencilState;
    RTHANDLETABLE hHTRasterizerState;
    RTHANDLETABLE hHTSamplerState;
    RTHANDLETABLE hHTElementLayout;
    RTHANDLETABLE hHTShader;
    RTHANDLETABLE hHTShaderResourceView;
    RTHANDLETABLE hHTRenderTargetView;
    RTHANDLETABLE hHTDepthStencilView;
    RTHANDLETABLE hHTQuery;
    RTHANDLETABLE hHTUnorderedAccessView;
    RTHANDLETABLE hHTStreamOutput;
    RTHANDLETABLE hHTVideoProcessor;
    RTHANDLETABLE hHTVideoDecoderOutputView;
    RTHANDLETABLE hHTVideoDecoder;
    RTHANDLETABLE hHTVideoProcessorInputView;
    RTHANDLETABLE hHTVideoProcessorOutputView;

    /* Resources */
    RTLISTANCHOR                listResources;              /* All resources of this device, for cleanup. */
    RTLISTANCHOR                listDestroyedResources;     /* DestroyResource adds to this list. Flush actually deleted them. */
    RTLISTANCHOR                listStagingResources;       /* List of staging resources for uploads. */

    /* Shaders */
    RTLISTANCHOR                listShaders;                /* All shaders of this device. */
    RTLISTANCHOR                listShadersAllocations;     /* Allocations with shaders bytescode and signatures. */

    /* Queries */
    RTLISTANCHOR                listQueries;                /* All queries of this device, to be able to repack them in the allocation. */
    RTLISTANCHOR                listCOAQuery;               /* List of COA VBOXDXKMRESOURCEs for all query types. */
    uint64_t volatile           u64MobFenceValue;

    /* Stream output declarations */
    RTLISTANCHOR                listCOAStreamOutput;        /* List of COA VBOXDXKMRESOURCEs for stream output declarations. */

    /* Pipeline state. */
    struct
    {
        uint32_t                cRenderTargetViews;
        PVBOXDXRENDERTARGETVIEW apRenderTargetViews[SVGA3D_MAX_SIMULTANEOUS_RENDER_TARGETS];

        PVBOXDXDEPTHSTENCILVIEW pDepthStencilView;

        uint32_t                cUnorderedAccessViews;

        VBOXDXCONSTANTBUFFERSSTATE aConstantBuffers[SVGA3D_SHADERTYPE_MAX - SVGA3D_SHADERTYPE_MIN]; /* For each shader type. */
        VBOXDXVERTEXBUFFERSSTATE   VertexBuffers;
        VBOXDXINDEXBUFFERSTATE     IndexBuffer;
        VBOXDXSRVSTATE             aSRVs[SVGA3D_SHADERTYPE_MAX - SVGA3D_SHADERTYPE_MIN]; /* For each shader type. */
    } pipeline;

    /* Video decoding and processing. */
    struct
    {
        PVBOXDXKMRESOURCE       pKMResource;                /* For querying capabilities of the host device. */
        uint64_t volatile       u64MobFenceValue;           /* Fence value for host queries. */

        bool                    fDecodeProfilesQueried;
        UINT                    cDecodeProfile;             /* Number of elements in paDecodeProfile array. */
        VBSVGA3dDecodeProfileInfo *paDecodeProfile;

        struct
        {
            uint32_t cConfig;                               /* Number of elements in pConfigInfo->aConfig array. */
            VBSVGA3dDecodeConfigInfo *pConfigInfo;
        } config;

        struct
        {
            VBSVGA3dVideoProcessorDesc desc;                /* Info has been queried for this desc; */
            VBSVGA3dVideoProcessorEnumInfo info;            /* Last queried info. */
        } videoProcessorEnum;
    } VideoDevice;
} VBOXDX_DEVICE, *PVBOXDX_DEVICE;

HRESULT ddi11_1RetrieveVideoFunctions(PVBOXDX_DEVICE pDevice, D3D11_1DDI_VIDEO_INPUT *pVideoInput);

HRESULT vboxDXDeviceInit(PVBOXDX_DEVICE pDevice);
void vboxDXDestroyDevice(PVBOXDX_DEVICE pDevice);

HRESULT vboxDXDeviceFlushCommands(PVBOXDX_DEVICE pDevice);
void *vboxDXCommandBufferReserve(PVBOXDX_DEVICE pDevice, SVGAFifo3dCmdId enmCmd, uint32_t cbCmd, uint32_t cPatchLocations = 0);
void vboxDXCommandBufferCommit(PVBOXDX_DEVICE pDevice);

void vboxDXStorePatchLocation(PVBOXDX_DEVICE pDevice, void *pvPatch, PVBOXDXKMRESOURCE pKMResource,
                              uint32_t offAllocation, bool fWriteOperation);

typedef void FNVBOXDXINITALLOCATIONDESC(VBOXDXALLOCATIONDESC *pDesc, void const *pvInitData);
typedef FNVBOXDXINITALLOCATIONDESC *PFNVBOXDXINITALLOCATIONDESC;
PVBOXDXKMRESOURCE vboxDXAllocateKMResource(PVBOXDX_DEVICE pDevice, HANDLE hResource,
                                           PFNVBOXDXINITALLOCATIONDESC pfnInitAllocationDesc,
                                           void const *pvInitData, bool fZero);
void vboxDXDeallocateKMResource(PVBOXDX_DEVICE pDevice, PVBOXDXKMRESOURCE pKMResource);

SVGA3dSurfaceFormat vboxDXDxgiToSvgaFormat(DXGI_FORMAT enmDxgiFormat);

bool vboxDXCreateResource(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource,
                          const D3D11DDIARG_CREATERESOURCE *pCreateResource);
bool vboxDXOpenResource(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource,
                        const D3D10DDIARG_OPENRESOURCE *pOpenResource);
void vboxDXDestroyResource(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource);

HRESULT vboxDXRotateResourceIdentities(PVBOXDX_DEVICE pDevice, UINT cResources, PVBOXDX_RESOURCE *papResources);
HRESULT vboxDXOfferResources(PVBOXDX_DEVICE pDevice, UINT cResources, PVBOXDX_RESOURCE *papResources, D3DDDI_OFFER_PRIORITY Priority);
HRESULT vboxDXReclaimResources(PVBOXDX_DEVICE pDevice, UINT cResources, PVBOXDX_RESOURCE *papResources, BOOL *paDiscarded);

void vboxDXCreateBlendState(PVBOXDX_DEVICE pDevice, PVBOXDX_BLENDSTATE pBlendState);
void vboxDXDestroyBlendState(PVBOXDX_DEVICE pDevice, PVBOXDX_BLENDSTATE pBlendState);
void vboxDXCreateDepthStencilState(PVBOXDX_DEVICE pDevice, PVBOXDX_DEPTHSTENCIL_STATE pDepthStencilState);
void vboxDXDestroyDepthStencilState(PVBOXDX_DEVICE pDevice, PVBOXDX_DEPTHSTENCIL_STATE pDepthStencilState);
void vboxDXCreateRasterizerState(PVBOXDX_DEVICE pDevice, PVBOXDX_RASTERIZER_STATE pRasterizerState);
void vboxDXDestroyRasterizerState(PVBOXDX_DEVICE pDevice, PVBOXDX_RASTERIZER_STATE pRasterizerState);
void vboxDXCreateSamplerState(PVBOXDX_DEVICE pDevice, PVBOXDX_SAMPLER_STATE pSamplerState);
void vboxDXDestroySamplerState(PVBOXDX_DEVICE pDevice, PVBOXDX_SAMPLER_STATE pSamplerState);
void vboxDXCreateElementLayout(PVBOXDX_DEVICE pDevice, PVBOXDXELEMENTLAYOUT pElementLayout);
void vboxDXDestroyElementLayout(PVBOXDX_DEVICE pDevice, PVBOXDXELEMENTLAYOUT pElementLayout);
void vboxDXSetInputLayout(PVBOXDX_DEVICE pDevice, PVBOXDXELEMENTLAYOUT pInputLayout);
void vboxDXSetBlendState(PVBOXDX_DEVICE pDevice, PVBOXDX_BLENDSTATE pBlendState,
                         const FLOAT BlendFactor[4], UINT SampleMask);
void vboxDXSetDepthStencilState(PVBOXDX_DEVICE pDevice, PVBOXDX_DEPTHSTENCIL_STATE pDepthStencilState, UINT StencilRef);
void vboxDXSetRasterizerState(PVBOXDX_DEVICE pDevice, PVBOXDX_RASTERIZER_STATE pRasterizerState);
void vboxDXSetSamplers(PVBOXDX_DEVICE pDevice, SVGA3dShaderType enmShaderType,
                       UINT StartSlot, UINT NumSamplers, const uint32_t *paSamplerIds);
void vboxDXIaSetTopology(PVBOXDX_DEVICE pDevice, D3D10_DDI_PRIMITIVE_TOPOLOGY PrimitiveTopology);
void vboxDXDrawIndexed(PVBOXDX_DEVICE pDevice, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation);
void vboxDXDraw(PVBOXDX_DEVICE pDevice, UINT VertexCount, UINT StartVertexLocation);
void vboxDXDrawIndexedInstanced(PVBOXDX_DEVICE pDevice, UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation);
void vboxDXDrawInstanced(PVBOXDX_DEVICE pDevice, UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation);
void vboxDXDrawAuto(PVBOXDX_DEVICE pDevice);
void vboxDXSetViewports(PVBOXDX_DEVICE pDevice, UINT NumViewports, UINT ClearViewports, const D3D10_DDI_VIEWPORT *pViewports);
void vboxDXSetScissorRects(PVBOXDX_DEVICE pDevice, UINT NumRects, UINT ClearRects, const D3D10_DDI_RECT *pRects);
void vboxDXCreateShader(PVBOXDX_DEVICE pDevice, SVGA3dShaderType enmShaderType, PVBOXDXSHADER pShader, const UINT* pShaderCode,
                        const D3D11_1DDIARG_SIGNATURE_ENTRY2* pInputSignature, UINT NumInputSignatureEntries,
                        const D3D11_1DDIARG_SIGNATURE_ENTRY2* pOutputSignature, UINT NumOutputSignatureEntries,
                        const D3D11_1DDIARG_SIGNATURE_ENTRY2* pPatchConstantSignature, UINT NumPatchConstantSignatureEntries);
void vboxDXCreateStreamOutput(PVBOXDX_DEVICE pDevice, PVBOXDXSHADER pShader,
                              const D3D11DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY *pOutputStreamDecl, UINT NumEntries,
                              const UINT  *BufferStridesInBytes, UINT NumStrides,
                              UINT RasterizedStream);
void vboxDXDestroyShader(PVBOXDX_DEVICE pDevice, PVBOXDXSHADER pShader);
void vboxDXCreateQuery(PVBOXDX_DEVICE pDevice, PVBOXDXQUERY pQuery, D3D10DDI_QUERY Query, UINT MiscFlags);
void vboxDXDestroyQuery(PVBOXDX_DEVICE pDevice, PVBOXDXQUERY pQuery);
void vboxDXQueryBegin(PVBOXDX_DEVICE pDevice, PVBOXDXQUERY pQuery);
void vboxDXQueryEnd(PVBOXDX_DEVICE pDevice, PVBOXDXQUERY pQuery);
void vboxDXQueryGetData(PVBOXDX_DEVICE pDevice, PVBOXDXQUERY pQuery, VOID* pData, UINT DataSize, UINT Flags);
void vboxDXSetPredication(PVBOXDX_DEVICE pDevice, PVBOXDXQUERY pQuery, BOOL PredicateValue);
void vboxDXSetShader(PVBOXDX_DEVICE pDevice, SVGA3dShaderType enmShaderType, PVBOXDXSHADER pShader);
void vboxDXSetVertexBuffers(PVBOXDX_DEVICE pDevice, UINT StartSlot, UINT NumBuffers,
                            PVBOXDX_RESOURCE *papBuffers, const UINT *pStrides, const UINT *pOffsets);
void vboxDXSetIndexBuffer(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pBuffer, DXGI_FORMAT Format, UINT Offset);
void vboxDXResourceUpdateSubresourceUP(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pDstResource,
                                       UINT DstSubresource, const D3D10_DDI_BOX *pDstBox,
                                       const VOID *pSysMemUP, UINT RowPitch, UINT DepthPitch, UINT CopyFlags);
void vboxDXResourceMap(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource, UINT Subresource,
                              D3D10_DDI_MAP DDIMap, UINT Flags, D3D10DDI_MAPPED_SUBRESOURCE *pMappedSubResource);
void vboxDXResourceUnmap(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource, UINT Subresource);
void vboxDXSoSetTargets(PVBOXDX_DEVICE pDevice, uint32_t NumTargets, PVBOXDXKMRESOURCE *papKMResources, uint32_t *paOffsets, uint32_t *paSizes);
void vboxDXCreateShaderResourceView(PVBOXDX_DEVICE pDevice, PVBOXDXSHADERRESOURCEVIEW pShaderResourceView);
void vboxDXGenMips(PVBOXDX_DEVICE pDevice, PVBOXDXSHADERRESOURCEVIEW pShaderResourceView);
void vboxDXDestroyShaderResourceView(PVBOXDX_DEVICE pDevice, PVBOXDXSHADERRESOURCEVIEW pShaderResourceView);
void vboxDXCreateRenderTargetView(PVBOXDX_DEVICE pDevice, PVBOXDXRENDERTARGETVIEW pRenderTargetView);
void vboxDXClearRenderTargetView(PVBOXDX_DEVICE pDevice, PVBOXDXRENDERTARGETVIEW pRenderTargetView, const FLOAT ColorRGBA[4]);
void vboxDXClearRenderTargetViewRegion(PVBOXDX_DEVICE pDevice, PVBOXDXRENDERTARGETVIEW pRenderTargetView, const FLOAT Color[4], const D3D10_DDI_RECT *pRect, UINT NumRects);
void vboxDXDestroyRenderTargetView(PVBOXDX_DEVICE pDevice, PVBOXDXRENDERTARGETVIEW pRenderTargetView);
void vboxDXCreateDepthStencilView(PVBOXDX_DEVICE pDevice, PVBOXDXDEPTHSTENCILVIEW pDepthStencilView);
void vboxDXClearDepthStencilView(PVBOXDX_DEVICE pDevice, PVBOXDXDEPTHSTENCILVIEW pDepthStencilView,
                                 UINT Flags, FLOAT Depth, UINT8 Stencil);
void vboxDXDestroyDepthStencilView(PVBOXDX_DEVICE pDevice, PVBOXDXDEPTHSTENCILVIEW pDepthStencilView);
void vboxDXSetRenderTargets(PVBOXDX_DEVICE pDevice, PVBOXDXDEPTHSTENCILVIEW pDepthStencilView,
                            uint32_t NumRTVs, UINT ClearSlots, PVBOXDXRENDERTARGETVIEW *papRenderTargetViews);
void vboxDXSetShaderResourceViews(PVBOXDX_DEVICE pDevice, SVGA3dShaderType enmShaderType, uint32_t StartSlot,
                                  uint32_t NumViews, PVBOXDXSHADERRESOURCEVIEW const *papViews);
void vboxDXSetConstantBuffers(PVBOXDX_DEVICE pDevice, SVGA3dShaderType enmShaderType, UINT StartSlot, UINT NumBuffers,
                              PVBOXDX_RESOURCE *papBuffers, const UINT *pFirstConstant, const UINT *pNumConstants);
void vboxDXResourceCopyRegion(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pDstResource, UINT DstSubresource,
                              UINT DstX, UINT DstY, UINT DstZ, PVBOXDX_RESOURCE pSrcResource, UINT SrcSubresource,
                              const D3D10_DDI_BOX *pSrcBox, UINT CopyFlags);
void vboxDXResourceCopy(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pDstResource, PVBOXDX_RESOURCE pSrcResource);
void vboxDXResourceResolveSubresource(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pDstResource, UINT DstSubresource,
                                      PVBOXDX_RESOURCE pSrcResource, UINT SrcSubresource, DXGI_FORMAT ResolveFormat);
void vboxDXCreateUnorderedAccessView(PVBOXDX_DEVICE pDevice, PVBOXDXUNORDEREDACCESSVIEW pUnorderedAccessView);
void vboxDXDestroyUnorderedAccessView(PVBOXDX_DEVICE pDevice, PVBOXDXUNORDEREDACCESSVIEW pUnorderedAccessView);
void vboxDXClearUnorderedAccessViewUint(PVBOXDX_DEVICE pDevice, PVBOXDXUNORDEREDACCESSVIEW pUnorderedAccessView, const UINT Values[4]);
void vboxDXClearUnorderedAccessViewFloat(PVBOXDX_DEVICE pDevice, PVBOXDXUNORDEREDACCESSVIEW pUnorderedAccessView, const FLOAT Values[4]);
void vboxDXSetUnorderedAccessViews(PVBOXDX_DEVICE pDevice, UINT StartSlot, UINT NumViews, const PVBOXDXUNORDEREDACCESSVIEW *papViews, const UINT *pUAVInitialCounts);
void vboxDXCsSetUnorderedAccessViews(PVBOXDX_DEVICE pDevice, UINT StartSlot, UINT NumViews, const uint32_t *paViewIds, const UINT* pUAVInitialCounts);
void vboxDXDispatch(PVBOXDX_DEVICE pDevice, UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ);
void vboxDXDispatchIndirect(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource, UINT AlignedByteOffsetForArgs);
void vboxDXDrawIndexedInstancedIndirect(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource, UINT AlignedByteOffsetForArgs);
void vboxDXDrawInstancedIndirect(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pResource, UINT AlignedByteOffsetForArgs);
void vboxDXCopyStructureCount(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pDstBuffer, UINT DstAlignedByteOffset, PVBOXDXUNORDEREDACCESSVIEW pSrcView);
void vboxDXClearView(PVBOXDX_DEVICE pDevice, D3D11DDI_HANDLETYPE ViewType, uint32_t ViewId, FLOAT const Color[4], D3D10_DDI_RECT const *pRect, UINT NumRects);
HRESULT vboxDXBlt(PVBOXDX_DEVICE pDevice, PVBOXDX_RESOURCE pDstResource, UINT DstSubresource,
                  PVBOXDX_RESOURCE pSrcResource, UINT SrcSubresource,
                  UINT DstLeft, UINT DstTop, UINT DstRight, UINT DstBottom,
                  DXGI_DDI_ARG_BLT_FLAGS Flags, DXGI_DDI_MODE_ROTATION Rotate);
HRESULT vboxDXFlush(PVBOXDX_DEVICE pDevice, bool fForce);

void vboxDXGetVideoDecoderProfileCount(PVBOXDX_DEVICE pDevice, UINT *pDecodeProfileCount);
void vboxDXGetVideoDecoderProfile(PVBOXDX_DEVICE pDevice, UINT Index, GUID *pGuid);
void vboxDXCheckVideoDecoderFormat(PVBOXDX_DEVICE pDevice, GUID const *pDecodeProfile, DXGI_FORMAT Format, BOOL *pSupported);
void vboxDXGetVideoDecoderConfigCount(PVBOXDX_DEVICE pDevice, D3D11_1DDI_VIDEO_DECODER_DESC const *pDecodeDesc, UINT *pConfigCount);
void vboxDXGetVideoDecoderConfig(PVBOXDX_DEVICE pDevice, D3D11_1DDI_VIDEO_DECODER_DESC const *pDecodeDesc, UINT Index,
                                 D3D11_1DDI_VIDEO_DECODER_CONFIG *pConfig);
HRESULT vboxDXCreateVideoProcessorEnum(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum,
                                       D3D11_1DDI_VIDEO_PROCESSOR_CONTENT_DESC const *Desc);
void vboxDXCheckVideoProcessorFormat(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum,
                                     DXGI_FORMAT Format, UINT *pSupported);
void vboxDXGetVideoProcessorCaps(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum,
                                 D3D11_1DDI_VIDEO_PROCESSOR_CAPS *pCaps);
void vboxDXGetVideoProcessorRateConversionCaps(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum,
                                               D3D11_1DDI_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS *pCaps);
void vboxDXGetVideoProcessorCustomRate(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum,
                                       UINT CustomRateIndex, D3D11_1DDI_VIDEO_PROCESSOR_CUSTOM_RATE *pRate);
void vboxDXGetVideoProcessorFilterRange(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum,
                                        D3D11_1DDI_VIDEO_PROCESSOR_FILTER Filter, D3D11_1DDI_VIDEO_PROCESSOR_FILTER_RANGE *pFilterRange);
HRESULT vboxDXCreateVideoProcessor(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor,
                                   PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum, UINT RateConversionCapsIndex);
HRESULT vboxDXCreateVideoDecoderOutputView(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEODECODEROUTPUTVIEW pVideoDecoderOutputView, PVBOXDX_RESOURCE pResource,
                                           GUID const &DecodeProfile, UINT MipSlice, UINT FirstArraySlice, UINT ArraySize);
HRESULT vboxDXCreateVideoDecoder(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEODECODER pVideoDecoder,
                                 D3D11_1DDI_VIDEO_DECODER_DESC const &Desc, D3D11_1DDI_VIDEO_DECODER_CONFIG const &Config);
HRESULT vboxDXVideoDecoderBeginFrame(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEODECODER pVideoDecoder,
                                     PVBOXDXVIDEODECODEROUTPUTVIEW pVideoDecoderOutputView,
                                     void const *pContentKey, UINT ContentKeySize);
HRESULT vboxDXVideoDecoderSubmitBuffers(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEODECODER pVideoDecoder,
                                        UINT BufferCount, D3D11_1DDI_VIDEO_DECODER_BUFFER_DESC const *pBufferDesc);
HRESULT vboxDXVideoDecoderEndFrame(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEODECODER pVideoDecoder);
HRESULT vboxDXCreateVideoProcessorInputView(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSORINPUTVIEW pVideoProcessorInputView,
                                            PVBOXDX_RESOURCE pResource, PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum,
                                            UINT FourCC, UINT MipSlice, UINT FirstArraySlice, UINT ArraySize);
HRESULT vboxDXCreateVideoProcessorOutputView(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOROUTPUTVIEW pVideoProcessorOutputView,
                                             PVBOXDX_RESOURCE pResource, PVBOXDXVIDEOPROCESSORENUM pVideoProcessorEnum,
                                             UINT MipSlice, UINT FirstArraySlice, UINT ArraySize);
HRESULT vboxDXVideoProcessorBlt(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, PVBOXDXVIDEOPROCESSOROUTPUTVIEW pVideoProcessorOutputView,
                                UINT OutputFrame, UINT StreamCount, D3D11_1DDI_VIDEO_PROCESSOR_STREAM const *paStream);
void vboxDXDestroyVideoDecoder(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEODECODER pVideoDecoder);
void vboxDXDestroyVideoDecoderOutputView(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEODECODEROUTPUTVIEW pVideoDecoderOutputView);
void vboxDXDestroyVideoProcessor(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor);
void vboxDXDestroyVideoProcessorInputView(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSORINPUTVIEW pVideoProcessorInputView);
void vboxDXDestroyVideoProcessorOutputView(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOROUTPUTVIEW pVideoProcessorOutputView);
void vboxDXVideoProcessorSetOutputTargetRect(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, BOOL Enable, RECT const *pOutputRect);
void vboxDXVideoProcessorSetOutputBackgroundColor(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, BOOL YCbCr, D3D11_1DDI_VIDEO_COLOR const *pColor);
void vboxDXVideoProcessorSetOutputColorSpace(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, D3D11_1DDI_VIDEO_PROCESSOR_COLOR_SPACE const *pColorSpace);
void vboxDXVideoProcessorSetOutputAlphaFillMode(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, D3D11_1DDI_VIDEO_PROCESSOR_ALPHA_FILL_MODE FillMode, UINT StreamIndex);
void vboxDXVideoProcessorSetOutputConstriction(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, BOOL Enabled, SIZE ConstrictonSize);
void vboxDXVideoProcessorSetOutputStereoMode(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, BOOL Enable);
void vboxDXVideoProcessorSetStreamFrameFormat(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex, D3D11_1DDI_VIDEO_FRAME_FORMAT Format);
void vboxDXVideoProcessorSetStreamColorSpace(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex, D3D11_1DDI_VIDEO_PROCESSOR_COLOR_SPACE const *pColorSpace);
void vboxDXVideoProcessorSetStreamOutputRate(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex,
                                             D3D11_1DDI_VIDEO_PROCESSOR_OUTPUT_RATE OutputRate, BOOL RepeatFrame, DXGI_RATIONAL const *pCustomRate);
void vboxDXVideoProcessorSetStreamSourceRect(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex,
                                             BOOL Enable, RECT const *pSourceRect);
void vboxDXVideoProcessorSetStreamDestRect(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex,
                                           BOOL Enable, RECT const *pDestRect);
void vboxDXVideoProcessorSetStreamAlpha(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex,
                                        BOOL Enable, FLOAT Alpha);
void vboxDXVideoProcessorSetStreamPalette(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex,
                                          UINT Count, UINT const *pEntries);
void vboxDXVideoProcessorSetStreamPixelAspectRatio(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex,
                                                   BOOL Enable, DXGI_RATIONAL const *pSourceRatio, DXGI_RATIONAL const *pDestRatio);
void vboxDXVideoProcessorSetStreamLumaKey(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex,
                                          BOOL Enable, FLOAT Lower, FLOAT Upper);
void vboxDXVideoProcessorSetStreamStereoFormat(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex, BOOL Enable,
                                               D3D11_1DDI_VIDEO_PROCESSOR_STEREO_FORMAT StereoFormat, BOOL LeftViewFrame0, BOOL BaseViewFrame0,
                                               D3D11_1DDI_VIDEO_PROCESSOR_STEREO_FLIP_MODE FlipMode, int MonoOffset);
void vboxDXVideoProcessorSetStreamAutoProcessingMode(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex, BOOL Enable);
void vboxDXVideoProcessorSetStreamFilter(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex, BOOL Enable,
                                         D3D11_1DDI_VIDEO_PROCESSOR_FILTER Filter, int Level);
void vboxDXVideoProcessorSetStreamRotation(PVBOXDX_DEVICE pDevice, PVBOXDXVIDEOPROCESSOR pVideoProcessor, UINT StreamIndex, BOOL Enable,
                                           D3D11_1DDI_VIDEO_PROCESSOR_ROTATION Rotation);

DECLINLINE(void) vboxDXDeviceSetError(PVBOXDX_DEVICE pDevice, HRESULT hr)
{
    Assert(hr == DXGI_DDI_ERR_WASSTILLDRAWING);
    pDevice->pUMCallbacks->pfnSetErrorCb(pDevice->hRTCoreLayer, hr);
}

DECLINLINE(PVBOXDXKMRESOURCE) vboxDXGetKMResource(PVBOXDX_RESOURCE pResource)
{
    return pResource ? pResource->pKMResource : 0;
}

DECLINLINE(D3DKMT_HANDLE) vboxDXGetAllocation(PVBOXDXKMRESOURCE pKMResource)
{
    return pKMResource ? pKMResource->hAllocation : 0;
}

DECLINLINE(D3DKMT_HANDLE) vboxDXGetAllocation(PVBOXDX_RESOURCE pResource)
{
    return pResource ? vboxDXGetAllocation(pResource->pKMResource) : 0;
}

DECLINLINE(VBOXDXALLOCATIONDESC *) vboxDXGetAllocationDesc(PVBOXDXKMRESOURCE pKMResource)
{
    return pKMResource ? &pKMResource->AllocationDesc : 0;
}

DECLINLINE(VBOXDXALLOCATIONDESC *) vboxDXGetAllocationDesc(PVBOXDX_RESOURCE pResource)
{
    return pResource ? vboxDXGetAllocationDesc(pResource->pKMResource) : 0;
}

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_dx_VBoxDX_h */
