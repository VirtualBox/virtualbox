/* $Id: VBoxDispD3D.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxVideo Display D3D User mode dll
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_VBoxDispD3D_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_VBoxDispD3D_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBoxDispD3DIf.h"
#include "../../common/wddm/VBoxMPIf.h"

#include <iprt/cdefs.h>
#include <iprt/list.h>

#define VBOXWDDMDISP_MAX_VERTEX_STREAMS 16
#define VBOXWDDMDISP_MAX_TEX_SAMPLERS 16
#define VBOXWDDMDISP_TOTAL_SAMPLERS VBOXWDDMDISP_MAX_TEX_SAMPLERS + 5
#define VBOXWDDMDISP_SAMPLER_IDX_IS_SPECIAL(_i) ((_i) >= D3DDMAPSAMPLER && (_i) <= D3DVERTEXTEXTURESAMPLER3)
#define VBOXWDDMDISP_SAMPLER_IDX_SPECIAL(_i) (VBOXWDDMDISP_SAMPLER_IDX_IS_SPECIAL(_i) ? (int)((_i) - D3DDMAPSAMPLER + VBOXWDDMDISP_MAX_TEX_SAMPLERS) : (int)-1)
#define VBOXWDDMDISP_SAMPLER_IDX(_i) (((_i) < VBOXWDDMDISP_MAX_TEX_SAMPLERS) ? (int)(_i) : VBOXWDDMDISP_SAMPLER_IDX_SPECIAL(_i))


/* maximum number of direct render targets to be used before
 * switching to offscreen rendering */
#ifdef VBOXWDDMDISP_DEBUG
# define VBOXWDDMDISP_MAX_DIRECT_RTS      g_VBoxVDbgCfgMaxDirectRts
#else
# define VBOXWDDMDISP_MAX_DIRECT_RTS      3
#endif

#define VBOXWDDMDISP_IS_TEXTURE(_f) ((_f).Texture || (_f).Value == 0)

#ifdef VBOX_WITH_VIDEOHWACCEL
typedef struct VBOXDISPVHWA_INFO
{
    VBOXVHWA_INFO Settings;
}VBOXDISPVHWA_INFO;

/* represents settings secific to
 * display device (head) on the multiple-head graphics card
 * currently used for 2D (overlay) only since in theory its settings
 * can differ per each frontend's framebuffer. */
typedef struct VBOXWDDMDISP_HEAD
{
    VBOXDISPVHWA_INFO Vhwa;
} VBOXWDDMDISP_HEAD;
#endif

typedef struct VBOXWDDMDISP_ADAPTER
{
    HANDLE hAdapter;
    UINT uIfVersion;
    UINT uRtVersion;
    D3DDDI_ADAPTERCALLBACKS RtCallbacks;

    VBOXVIDEO_HWTYPE enmHwType;     /* VBOXVIDEO_HWTYPE_* */

    VBOXWDDMDISP_D3D D3D;
    VBOXWDDMDISP_FORMATS Formats;
    uint32_t u32VBox3DCaps;
    bool f3D;
    bool fReserved[3];

    VBOXWDDM_QAI AdapterInfo;

#ifdef VBOX_WITH_VIDEOHWACCEL
    uint32_t cHeads;
    VBOXWDDMDISP_HEAD aHeads[1];
#endif
} VBOXWDDMDISP_ADAPTER, *PVBOXWDDMDISP_ADAPTER;

typedef struct VBOXWDDMDISP_CONTEXT
{
    RTLISTNODE ListNode;
    struct VBOXWDDMDISP_DEVICE *pDevice;
    D3DDDICB_CREATECONTEXT ContextInfo;
} VBOXWDDMDISP_CONTEXT, *PVBOXWDDMDISP_CONTEXT;

typedef struct VBOXWDDMDISP_STREAMSOURCEUM
{
    CONST VOID* pvBuffer;
    UINT cbStride;
} VBOXWDDMDISP_STREAMSOURCEUM, *PVBOXWDDMDISP_STREAMSOURCEUM;

typedef struct VBOXWDDMDISP_INDICIESUM
{
    CONST VOID* pvBuffer;
    UINT cbSize;
} VBOXWDDMDISP_INDICIESUM, *PVBOXWDDMDISP_INDICIESUM;

struct VBOXWDDMDISP_ALLOCATION;

typedef struct VBOXWDDMDISP_STREAM_SOURCE_INFO
{
  UINT   uiOffset;
  UINT   uiStride;
} VBOXWDDMDISP_STREAM_SOURCE_INFO;

typedef struct VBOXWDDMDISP_INDICES_INFO
{
    struct VBOXWDDMDISP_ALLOCATION *pIndicesAlloc;
    const void *pvIndicesUm;
    UINT uiStride;
} VBOXWDDMDISP_INDICES_INFO;

typedef struct VBOXWDDMDISP_RENDERTGT_FLAGS
{
    union
    {
        struct
        {
            UINT bAdded : 1;
            UINT bRemoved : 1;
            UINT Reserved : 30;
        };
        uint32_t Value;
    };
}VBOXWDDMDISP_RENDERTGT_FLAGS;

typedef struct VBOXWDDMDISP_RENDERTGT
{
    struct VBOXWDDMDISP_ALLOCATION *pAlloc;
    UINT cNumFlips;
    VBOXWDDMDISP_RENDERTGT_FLAGS fFlags;
} VBOXWDDMDISP_RENDERTGT, *PVBOXWDDMDISP_RENDERTGT;

typedef struct VBOXWDDMDISP_DEVICE *PVBOXWDDMDISP_DEVICE;
typedef HRESULT FNVBOXWDDMCREATEDIRECT3DDEVICE(PVBOXWDDMDISP_DEVICE pDevice);
typedef FNVBOXWDDMCREATEDIRECT3DDEVICE *PFNVBOXWDDMCREATEDIRECT3DDEVICE;

typedef IUnknown* FNVBOXWDDMCREATESHAREDPRIMARY(struct VBOXWDDMDISP_ALLOCATION *pAlloc);
typedef FNVBOXWDDMCREATESHAREDPRIMARY *PFNVBOXWDDMCREATESHAREDPRIMARY;

typedef struct VBOXWDDMDISP_DEVICE
{
    HANDLE hDevice;
    PVBOXWDDMDISP_ADAPTER pAdapter;
    PFNVBOXWDDMCREATEDIRECT3DDEVICE pfnCreateDirect3DDevice;
    PFNVBOXWDDMCREATESHAREDPRIMARY pfnCreateSharedPrimary;
    IDirect3DDevice9 *pDevice9If;
    UINT u32IfVersion;
    UINT uRtVersion;
    D3DDDI_DEVICECALLBACKS RtCallbacks;
    VOID *pvCmdBuffer;
    UINT cbCmdBuffer;
    D3DDDI_CREATEDEVICEFLAGS fFlags;
    /* number of StreamSources set */
    UINT cStreamSources;
    UINT cStreamSourcesUm;
    VBOXWDDMDISP_STREAMSOURCEUM aStreamSourceUm[VBOXWDDMDISP_MAX_VERTEX_STREAMS];
    struct VBOXWDDMDISP_ALLOCATION *aStreamSource[VBOXWDDMDISP_MAX_VERTEX_STREAMS];
    VBOXWDDMDISP_STREAM_SOURCE_INFO StreamSourceInfo[VBOXWDDMDISP_MAX_VERTEX_STREAMS];
    VBOXWDDMDISP_INDICES_INFO IndiciesInfo;
    /* Need to cache the ViewPort data because IDirect3DDevice9::SetViewport
     * is split into two calls: SetViewport & SetZRange.
     * Also the viewport must be restored after IDirect3DDevice9::SetRenderTarget.
     */
    D3DVIEWPORT9 ViewPort;
    /* The scissor rectangle must be restored after IDirect3DDevice9::SetRenderTarget. */
    RECT ScissorRect;
    /* Whether the ViewPort field is valid, i.e. GaDdiSetViewport has been called. */
    bool fViewPort : 1;
    /* Whether the ScissorRect field is valid, i.e. GaDdiSetScissorRect has been called. */
    bool fScissorRect : 1;
    VBOXWDDMDISP_CONTEXT DefaultContext;

    /* no lock is needed for this since we're guaranteed the per-device calls are not reentrant */
    RTLISTANCHOR DirtyAllocList;

    UINT cSamplerTextures;
    struct VBOXWDDMDISP_RESOURCE *aSamplerTextures[VBOXWDDMDISP_TOTAL_SAMPLERS];

    struct VBOXWDDMDISP_RESOURCE *pDepthStencilRc;

    HMODULE hHgsmiTransportModule;

    UINT cRTs;
    struct VBOXWDDMDISP_ALLOCATION * apRTs[1];
} VBOXWDDMDISP_DEVICE, *PVBOXWDDMDISP_DEVICE;

typedef struct VBOXWDDMDISP_LOCKINFO
{
    uint32_t cLocks;
    union {
        D3DDDIRANGE  Range;
        RECT  Area;
        D3DDDIBOX  Box;
    };
    D3DDDI_LOCKFLAGS fFlags;
    union {
        D3DLOCKED_RECT LockedRect;
        D3DLOCKED_BOX LockedBox;
    };
#ifdef VBOXWDDMDISP_DEBUG
    PVOID pvData;
#endif
} VBOXWDDMDISP_LOCKINFO;

typedef enum
{
    VBOXDISP_D3DIFTYPE_UNDEFINED = 0,
    VBOXDISP_D3DIFTYPE_SURFACE,
    VBOXDISP_D3DIFTYPE_TEXTURE,
    VBOXDISP_D3DIFTYPE_CUBE_TEXTURE,
    VBOXDISP_D3DIFTYPE_VOLUME_TEXTURE,
    VBOXDISP_D3DIFTYPE_VERTEXBUFFER,
    VBOXDISP_D3DIFTYPE_INDEXBUFFER
} VBOXDISP_D3DIFTYPE;

typedef struct VBOXWDDMDISP_ALLOCATION
{
    D3DKMT_HANDLE hAllocation;
    VBOXWDDM_ALLOC_TYPE enmType;
    UINT iAlloc;
    struct VBOXWDDMDISP_RESOURCE *pRc;
    void* pvMem;
    /* object type is defined by enmD3DIfType enum */
    IUnknown *pD3DIf;
    VBOXDISP_D3DIFTYPE enmD3DIfType;
    /* list entry used to add allocation to the dirty alloc list */
    RTLISTNODE DirtyAllocListEntry;
    BOOLEAN fEverWritten;
    BOOLEAN fDirtyWrite;
    BOOLEAN fAllocLocked;
    HANDLE hSharedHandle;
    VBOXWDDMDISP_LOCKINFO LockInfo;
    VBOXWDDM_DIRTYREGION DirtyRegion; /* <- dirty region to notify host about */
    VBOXWDDM_SURFACE_DESC SurfDesc;
#ifdef VBOX_WITH_MESA3D
    uint32_t hostID;
#endif
#ifdef VBOX_WITH_VMSVGA3D_DX9
    VBOXDXALLOCATIONDESC AllocDesc;
#endif
} VBOXWDDMDISP_ALLOCATION, *PVBOXWDDMDISP_ALLOCATION;

typedef struct VBOXWDDMDISP_RESOURCE
{
    HANDLE hResource;
    D3DKMT_HANDLE hKMResource;
    PVBOXWDDMDISP_DEVICE pDevice;
    VBOXWDDMDISP_RESOURCE_FLAGS fFlags;
    VBOXWDDM_RC_DESC RcDesc;
    UINT cAllocations;
    VBOXWDDMDISP_ALLOCATION aAllocations[1];
} VBOXWDDMDISP_RESOURCE, *PVBOXWDDMDISP_RESOURCE;

typedef struct VBOXWDDMDISP_QUERY
{
    D3DDDIQUERYTYPE enmType;
    D3DDDI_ISSUEQUERYFLAGS fQueryState;
    IDirect3DQuery9 *pQueryIf;
} VBOXWDDMDISP_QUERY, *PVBOXWDDMDISP_QUERY;

typedef struct VBOXWDDMDISP_TSS_LOOKUP
{
    BOOL  bSamplerState;
    DWORD dType;
} VBOXWDDMDISP_TSS_LOOKUP;

typedef struct VBOXWDDMDISP_OVERLAY
{
    D3DKMT_HANDLE hOverlay;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    PVBOXWDDMDISP_RESOURCE *pResource;
} VBOXWDDMDISP_OVERLAY, *PVBOXWDDMDISP_OVERLAY;

#define VBOXDISP_CUBEMAP_LEVELS_COUNT(pRc) (((pRc)->cAllocations)/6)
#define VBOXDISP_CUBEMAP_INDEX_TO_FACE(pRc, idx) ((D3DCUBEMAP_FACES)(D3DCUBEMAP_FACE_POSITIVE_X+(idx)/VBOXDISP_CUBEMAP_LEVELS_COUNT(pRc)))
#define VBOXDISP_CUBEMAP_INDEX_TO_LEVEL(pRc, idx) ((idx)%VBOXDISP_CUBEMAP_LEVELS_COUNT(pRc))

void vboxWddmResourceInit(PVBOXWDDMDISP_RESOURCE pRc, UINT cAllocs);

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_VBoxDispD3D_h */
