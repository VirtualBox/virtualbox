/* $Id: GaDrvEnvKMT.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - Gallium driver interface to the WDDM miniport driver using Kernel Mode Thunks.
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

#include "GaDrvEnvKMT.h"

#include <UmHlpInternal.h>

#include "svga3d_reg.h"

#include <common/wddm/VBoxMPIf.h>

#include <iprt/assertcompile.h>
#include <iprt/param.h> /* For PAGE_SIZE */

AssertCompile(sizeof(HANDLE) >= sizeof(D3DKMT_HANDLE));


/*
 * AVL configuration.
 */
#define KAVL_FN(a)                  RTAvlU32##a
#define KAVL_MAX_STACK              27  /* Up to 2^24 nodes. */
#define KAVL_CHECK_FOR_EQUAL_INSERT 1   /* No duplicate keys! */
#define KAVLNODECORE                AVLU32NODECORE
#define PKAVLNODECORE               PAVLU32NODECORE
#define PPKAVLNODECORE              PPAVLU32NODECORE
#define KAVLKEY                     AVLU32KEY
#define PKAVLKEY                    PAVLU32KEY
#define KAVLENUMDATA                AVLU32ENUMDATA
#define PKAVLENUMDATA               PAVLU32ENUMDATA
#define PKAVLCALLBACK               PAVLU32CALLBACK


/*
 * AVL Compare macros
 */
#define KAVL_G(key1, key2)          ( (key1) >  (key2) )
#define KAVL_E(key1, key2)          ( (key1) == (key2) )
#define KAVL_NE(key1, key2)         ( (key1) != (key2) )


#include <iprt/avl.h>

/*
 * Include the code.
 */
#define SSToDS(ptr) ptr
#define KMAX RT_MAX
#define kASSERT(_e) do { } while (0)
#include "avl_Base.cpp.h"
#include "avl_Get.cpp.h"
//#include "avl_GetBestFit.cpp.h"
//#include "avl_RemoveBestFit.cpp.h"
//#include "avl_DoWithAll.cpp.h"
#include "avl_Destroy.cpp.h"


typedef struct GaKmtCallbacks
{
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE hDevice;
    D3DKMTFUNCTIONS const *d3dkmt;
    LUID AdapterLuid;
} GaKmtCallbacks;

class GaDrvEnvKmt
{
    public:
        GaDrvEnvKmt();
        ~GaDrvEnvKmt();

        HRESULT Init(void);

        const WDDMGalliumDriverEnv *Env();

        /*
         * KMT specific helpers.
         */
        bool drvEnvKmtRenderCompose(uint32_t u32Cid,
                                    void *pvCommands,
                                    uint32_t cbCommands,
                                    ULONGLONG PresentHistoryToken);
        D3DKMT_HANDLE drvEnvKmtContextHandle(uint32_t u32Cid);
        D3DKMT_HANDLE drvEnvKmtSurfaceHandle(uint32_t u32Sid);

        GaKmtCallbacks mKmtCallbacks;

    private:

        VBOXGAHWINFO mHWInfo;

        /* Map to convert context id (cid) to WDDM context information (GAWDDMCONTEXTINFO).
         * Key is the 32 bit context id.
         */
        AVLU32TREE mContextTree;

        /* Map to convert surface id (sid) to WDDM surface information (GAWDDMSURFACEINFO).
         * Key is the 32 bit surface id.
         */
        AVLU32TREE mSurfaceTree;

        WDDMGalliumDriverEnv mEnv;

        static DECLCALLBACK(uint32_t) gaEnvContextCreate(void *pvEnv,
                                                         boolean extended,
                                                         boolean vgpu10);
        static DECLCALLBACK(void) gaEnvContextDestroy(void *pvEnv,
                                                      uint32_t u32Cid);
        static DECLCALLBACK(int) gaEnvSurfaceDefine(void *pvEnv,
                                                    GASURFCREATE *pCreateParms,
                                                    GASURFSIZE *paSizes,
                                                    uint32_t cSizes,
                                                    uint32_t *pu32Sid);
        static DECLCALLBACK(void) gaEnvSurfaceDestroy(void *pvEnv,
                                                      uint32_t u32Sid);
        static DECLCALLBACK(int) gaEnvRender(void *pvEnv,
                                             uint32_t u32Cid,
                                             void *pvCommands,
                                             uint32_t cbCommands,
                                             GAFENCEQUERY *pFenceQuery);
        static DECLCALLBACK(void) gaEnvFenceUnref(void *pvEnv,
                                                  uint32_t u32FenceHandle);
        static DECLCALLBACK(int) gaEnvFenceQuery(void *pvEnv,
                                                 uint32_t u32FenceHandle,
                                                 GAFENCEQUERY *pFenceQuery);
        static DECLCALLBACK(int) gaEnvFenceWait(void *pvEnv,
                                                uint32_t u32FenceHandle,
                                                uint32_t u32TimeoutUS);
        static DECLCALLBACK(int) gaEnvRegionCreate(void *pvEnv,
                                                   uint32_t u32RegionSize,
                                                   uint32_t *pu32GmrId,
                                                   void **ppvMap);
        static DECLCALLBACK(void) gaEnvRegionDestroy(void *pvEnv,
                                                     uint32_t u32GmrId,
                                                     void *pvMap);

        /* VGPU10 */
        static DECLCALLBACK(int) gaEnvGBSurfaceDefine(void *pvEnv,
                                                      SVGAGBSURFCREATE *pCreateParms);

        /*
         * Internal.
         */
        bool doRender(uint32_t u32Cid, void *pvCommands, uint32_t cbCommands,
                      GAFENCEQUERY *pFenceQuery, ULONGLONG PresentHistoryToken, bool fPresentRedirected);
};

typedef struct GAWDDMCONTEXTINFO
{
    AVLU32NODECORE            Core;
    D3DKMT_HANDLE             hContext;
    VOID                     *pCommandBuffer;
    UINT                      CommandBufferSize;
    D3DDDI_ALLOCATIONLIST    *pAllocationList;
    UINT                      AllocationListSize;
    D3DDDI_PATCHLOCATIONLIST *pPatchLocationList;
    UINT                      PatchLocationListSize;
} GAWDDMCONTEXTINFO;

typedef struct GAWDDMSURFACEINFO
{
    AVLU32NODECORE            Core;
    D3DKMT_HANDLE             hAllocation;
} GAWDDMSURFACEINFO;


/// @todo vboxDdi helpers must return a boof success indicator
static bool
vboxDdiQueryAdapterInfo(GaKmtCallbacks *pKmtCallbacks,
                        D3DKMT_HANDLE hAdapter,
                        VBOXWDDM_QAI *pAdapterInfo,
                        uint32_t cbAdapterInfo)
{
    D3DKMT_QUERYADAPTERINFO QAI;
    memset(&QAI, 0, sizeof(QAI));
    QAI.hAdapter              = hAdapter;
    QAI.Type                  = KMTQAITYPE_UMDRIVERPRIVATE;
    QAI.pPrivateDriverData    = pAdapterInfo;
    QAI.PrivateDriverDataSize = cbAdapterInfo;

    NTSTATUS Status = pKmtCallbacks->d3dkmt->pfnD3DKMTQueryAdapterInfo(&QAI);
    return Status == STATUS_SUCCESS;
}

static void
vboxDdiDeviceDestroy(GaKmtCallbacks *pKmtCallbacks,
                     D3DKMT_HANDLE hDevice)
{
    if (hDevice)
    {
        D3DKMT_DESTROYDEVICE DestroyDeviceData;
        memset(&DestroyDeviceData, 0, sizeof(DestroyDeviceData));
        DestroyDeviceData.hDevice = hDevice;
        pKmtCallbacks->d3dkmt->pfnD3DKMTDestroyDevice(&DestroyDeviceData);
    }
}

static bool
vboxDdiDeviceCreate(GaKmtCallbacks *pKmtCallbacks,
                    D3DKMT_HANDLE *phDevice)
{
    D3DKMT_CREATEDEVICE CreateDeviceData;
    memset(&CreateDeviceData, 0, sizeof(CreateDeviceData));
    CreateDeviceData.hAdapter = pKmtCallbacks->hAdapter;
    // CreateDeviceData.Flags = 0;

    NTSTATUS Status = pKmtCallbacks->d3dkmt->pfnD3DKMTCreateDevice(&CreateDeviceData);
    if (Status == STATUS_SUCCESS)
    {
        *phDevice = CreateDeviceData.hDevice;
        return true;
    }
    return false;
}

static bool
vboxDdiContextGetId(GaKmtCallbacks *pKmtCallbacks,
                    D3DKMT_HANDLE hContext,
                    uint32_t *pu32Cid)
{
    VBOXDISPIFESCAPE_GAGETCID data;
    memset(&data, 0, sizeof(data));
    data.EscapeHdr.escapeCode  = VBOXESC_GAGETCID;
    // data.EscapeHdr.cmdSpecific = 0;
    // data.u32Cid                = 0;

    /* If the user-mode display driver sets hContext to a non-NULL value, the driver must
     * have also set hDevice to a non-NULL value...
     */
    D3DKMT_ESCAPE EscapeData;
    memset(&EscapeData, 0, sizeof(EscapeData));
    EscapeData.hAdapter              = pKmtCallbacks->hAdapter;
    EscapeData.hDevice               = pKmtCallbacks->hDevice;
    EscapeData.Type                  = D3DKMT_ESCAPE_DRIVERPRIVATE;
    // EscapeData.Flags.HardwareAccess  = 0;
    EscapeData.pPrivateDriverData    = &data;
    EscapeData.PrivateDriverDataSize = sizeof(data);
    EscapeData.hContext              = hContext;

    NTSTATUS Status = pKmtCallbacks->d3dkmt->pfnD3DKMTEscape(&EscapeData);
    if (Status == STATUS_SUCCESS)
    {
        *pu32Cid = data.u32Cid;
        return true;
    }
    return false;
}

static void
vboxDdiContextDestroy(GaKmtCallbacks *pKmtCallbacks,
                      GAWDDMCONTEXTINFO *pContextInfo)
{
    if (pContextInfo->hContext)
    {
        D3DKMT_DESTROYCONTEXT DestroyContextData;
        memset(&DestroyContextData, 0, sizeof(DestroyContextData));
        DestroyContextData.hContext = pContextInfo->hContext;
        pKmtCallbacks->d3dkmt->pfnD3DKMTDestroyContext(&DestroyContextData);
    }
}

static bool
vboxDdiContextCreate(GaKmtCallbacks *pKmtCallbacks,
                     void *pvPrivateData, uint32_t cbPrivateData,
                     GAWDDMCONTEXTINFO *pContextInfo)
{
    D3DKMT_CREATECONTEXT CreateContextData;
    memset(&CreateContextData, 0, sizeof(CreateContextData));
    CreateContextData.hDevice = pKmtCallbacks->hDevice;
    // CreateContextData.NodeOrdinal = 0;
    // CreateContextData.EngineAffinity = 0;
    // CreateContextData.Flags.Value = 0;
    CreateContextData.pPrivateDriverData = pvPrivateData;
    CreateContextData.PrivateDriverDataSize = cbPrivateData;
    CreateContextData.ClientHint = D3DKMT_CLIENTHINT_OPENGL;

    NTSTATUS Status = pKmtCallbacks->d3dkmt->pfnD3DKMTCreateContext(&CreateContextData);
    if (Status == STATUS_SUCCESS)
    {
        /* Query cid. */
        uint32_t u32Cid = 0;
        bool fSuccess = vboxDdiContextGetId(pKmtCallbacks, CreateContextData.hContext, &u32Cid);
        if (fSuccess)
        {
            pContextInfo->Core.Key              = u32Cid;
            pContextInfo->hContext              = CreateContextData.hContext;
            pContextInfo->pCommandBuffer        = CreateContextData.pCommandBuffer;
            pContextInfo->CommandBufferSize     = CreateContextData.CommandBufferSize;
            pContextInfo->pAllocationList       = CreateContextData.pAllocationList;
            pContextInfo->AllocationListSize    = CreateContextData.AllocationListSize;
            pContextInfo->pPatchLocationList    = CreateContextData.pPatchLocationList;
            pContextInfo->PatchLocationListSize = CreateContextData.PatchLocationListSize;

            return true;
        }

        vboxDdiContextDestroy(pKmtCallbacks, pContextInfo);
    }

    return false;
}

/* static */ DECLCALLBACK(void)
GaDrvEnvKmt::gaEnvContextDestroy(void *pvEnv,
                                 uint32_t u32Cid)
{
    GaDrvEnvKmt *pThis = (GaDrvEnvKmt *)pvEnv;

    GAWDDMCONTEXTINFO *pContextInfo = (GAWDDMCONTEXTINFO *)RTAvlU32Remove(&pThis->mContextTree, u32Cid);
    if (pContextInfo)
    {
        vboxDdiContextDestroy(&pThis->mKmtCallbacks, pContextInfo);
        memset(pContextInfo, 0, sizeof(*pContextInfo));
        free(pContextInfo);
    }
}

D3DKMT_HANDLE GaDrvEnvKmt::drvEnvKmtContextHandle(uint32_t u32Cid)
{
    GAWDDMCONTEXTINFO *pContextInfo = (GAWDDMCONTEXTINFO *)RTAvlU32Get(&mContextTree, u32Cid);
    Assert(pContextInfo);
    return pContextInfo ? pContextInfo->hContext : 0;
}

/* static */ DECLCALLBACK(uint32_t)
GaDrvEnvKmt::gaEnvContextCreate(void *pvEnv,
                                boolean extended,
                                boolean vgpu10)
{
    GaDrvEnvKmt *pThis = (GaDrvEnvKmt *)pvEnv;

    GAWDDMCONTEXTINFO *pContextInfo;
    pContextInfo = (GAWDDMCONTEXTINFO *)malloc(sizeof(GAWDDMCONTEXTINFO));
    if (!pContextInfo)
        return (uint32_t)-1;

    VBOXWDDM_CREATECONTEXT_INFO privateData;
    memset(&privateData, 0, sizeof(privateData));
    privateData.u32IfVersion       = 9;
    privateData.enmType            = VBOXWDDM_CONTEXT_TYPE_GA_3D;
    privateData.u.vmsvga.u32Flags  = extended? VBOXWDDM_F_GA_CONTEXT_EXTENDED: 0;
    privateData.u.vmsvga.u32Flags |= vgpu10? VBOXWDDM_F_GA_CONTEXT_VGPU10: 0;

    bool fSuccess = vboxDdiContextCreate(&pThis->mKmtCallbacks,
                                         &privateData, sizeof(privateData), pContextInfo);
    if (fSuccess)
    {
        if (RTAvlU32Insert(&pThis->mContextTree, &pContextInfo->Core))
        {
            return pContextInfo->Core.Key;
        }

        vboxDdiContextDestroy(&pThis->mKmtCallbacks,
                              pContextInfo);
    }

    Assert(0);
    free(pContextInfo);
    return (uint32_t)-1;
}

static D3DDDIFORMAT svgaToD3DDDIFormat(SVGA3dSurfaceFormat format)
{
    /* The returning D3DDDIFMT_ value is used only to compute bpp, pitch, etc,
     * so there is not need for an exact match.
     */
    switch (format)
    {
        case SVGA3D_X8R8G8B8:       return D3DDDIFMT_X8R8G8B8;
        case SVGA3D_A8R8G8B8:       return D3DDDIFMT_A8R8G8B8;
        case SVGA3D_ALPHA8:         return D3DDDIFMT_A8;
        case SVGA3D_A4R4G4B4:       return D3DDDIFMT_A4R4G4B4;
        case SVGA3D_LUMINANCE8:     return D3DDDIFMT_L8;
        case SVGA3D_A1R5G5B5:       return D3DDDIFMT_A1R5G5B5;
        case SVGA3D_LUMINANCE8_ALPHA8: return D3DDDIFMT_A8L8;
        case SVGA3D_R5G6B5:         return D3DDDIFMT_R5G6B5;
        case SVGA3D_ARGB_S10E5:     return D3DDDIFMT_A16B16G16R16F;
        case SVGA3D_ARGB_S23E8:     return D3DDDIFMT_A32B32G32R32F;
        case SVGA3D_A8_UNORM:       return D3DDDIFMT_A8;
        case SVGA3D_B5G5R5A1_UNORM: return D3DDDIFMT_A1R5G5B5;

        case SVGA3D_B8G8R8X8_TYPELESS:
        case SVGA3D_B8G8R8X8_UNORM:     return D3DDDIFMT_X8R8G8B8;
        case SVGA3D_R16_FLOAT:          return D3DDDIFMT_R16F;
        case SVGA3D_R16G16_FLOAT:       return D3DDDIFMT_G16R16F;
        case SVGA3D_R16G16B16A16_FLOAT: return D3DDDIFMT_A16B16G16R16F;
        case SVGA3D_R32_FLOAT:          return D3DDDIFMT_R32F;
        case SVGA3D_R32G32_FLOAT:       return D3DDDIFMT_G32R32F;
        case SVGA3D_R32G32B32A32_FLOAT: return D3DDDIFMT_A32B32G32R32F;
        case SVGA3D_R8_TYPELESS:
        case SVGA3D_R8_SINT:
        case SVGA3D_R8_UINT:
        case SVGA3D_R8_SNORM:
        case SVGA3D_R8_UNORM:           return D3DDDIFMT_L8;
        case SVGA3D_R8G8_TYPELESS:
        case SVGA3D_R8G8_SINT:
        case SVGA3D_R8G8_UINT:
        case SVGA3D_R8G8_SNORM:
        case SVGA3D_R8G8_UNORM:         return D3DDDIFMT_A8L8;
        case SVGA3D_R8G8B8A8_TYPELESS:
        case SVGA3D_R8G8B8A8_SINT:
        case SVGA3D_R8G8B8A8_UINT:
        case SVGA3D_R8G8B8A8_SNORM:
        case SVGA3D_R8G8B8A8_UNORM:     return D3DDDIFMT_A8R8G8B8;
        case SVGA3D_R16_TYPELESS:
        case SVGA3D_R16_SINT:
        case SVGA3D_R16_UINT:
        case SVGA3D_R16_SNORM:
        case SVGA3D_R16_UNORM:          return D3DDDIFMT_L16;
        case SVGA3D_R16G16_TYPELESS:
        case SVGA3D_R16G16_SINT:
        case SVGA3D_R16G16_UINT:
        case SVGA3D_R16G16_SNORM:
        case SVGA3D_R16G16_UNORM:       return D3DDDIFMT_G16R16;
        case SVGA3D_R16G16B16A16_TYPELESS:
        case SVGA3D_R16G16B16A16_SINT:
        case SVGA3D_R16G16B16A16_UINT:
        case SVGA3D_R16G16B16A16_SNORM:
        case SVGA3D_R16G16B16A16_UNORM: return D3DDDIFMT_A16B16G16R16;
        case SVGA3D_R32_TYPELESS:
        case SVGA3D_R32_SINT:
        case SVGA3D_R32_UINT:           return D3DDDIFMT_R32F; /* Same size in bytes. */
        case SVGA3D_R32G32_TYPELESS:
        case SVGA3D_R32G32_SINT:
        case SVGA3D_R32G32_UINT:        return D3DDDIFMT_G32R32F; /* Same size in bytes. */
        case SVGA3D_R32G32B32A32_TYPELESS:
        case SVGA3D_R32G32B32A32_SINT:
        case SVGA3D_R32G32B32A32_UINT:  return D3DDDIFMT_A32B32G32R32F; /* Same size in bytes. */
        case SVGA3D_R10G10B10A2_TYPELESS:
        case SVGA3D_R10G10B10A2_UINT:
        case SVGA3D_R10G10B10A2_UNORM:  return D3DDDIFMT_A2B10G10R10;
        case SVGA3D_B5G6R5_UNORM:       return D3DDDIFMT_R5G6B5;
        case SVGA3D_R11G11B10_FLOAT:    return D3DDDIFMT_R32F;
        case SVGA3D_B8G8R8A8_UNORM:     return D3DDDIFMT_A8R8G8B8;
        default: break;
    }

    VBoxDispMpLoggerLogF("WDDM: EnvKMT: unsupported surface format %d\n", format);
    Assert(0);
    return D3DDDIFMT_UNKNOWN;
}

/* static */ DECLCALLBACK(int)
GaDrvEnvKmt::gaEnvSurfaceDefine(void *pvEnv,
                                GASURFCREATE *pCreateParms,
                                GASURFSIZE *paSizes,
                                uint32_t cSizes,
                                uint32_t *pu32Sid)
{
    GaDrvEnvKmt *pThis = (GaDrvEnvKmt *)pvEnv;

    D3DKMT_ESCAPE                     EscapeData;
    VBOXDISPIFESCAPE_GASURFACEDEFINE *pData;
    uint32_t                          cbAlloc;
    uint8_t                          *pu8Req;
    uint32_t                          cbReq;

    /* Size of the SVGA request data */
    cbReq = sizeof(GASURFCREATE) + cSizes * sizeof(GASURFSIZE);
    /* How much to allocate for WDDM escape data. */
    cbAlloc =   sizeof(VBOXDISPIFESCAPE_GASURFACEDEFINE)
              + cbReq;

    pData = (VBOXDISPIFESCAPE_GASURFACEDEFINE *)malloc(cbAlloc);
    if (!pData)
        return -1;

    pData->EscapeHdr.escapeCode  = VBOXESC_GASURFACEDEFINE;
    // pData->EscapeHdr.cmdSpecific = 0;
    // pData->u32Sid                = 0;
    pData->cbReq                 = cbReq;
    pData->cSizes                = cSizes;

    pu8Req = (uint8_t *)&pData[1];
    memcpy(pu8Req, pCreateParms, sizeof(GASURFCREATE));
    memcpy(&pu8Req[sizeof(GASURFCREATE)], paSizes, cSizes * sizeof(GASURFSIZE));

    memset(&EscapeData, 0, sizeof(EscapeData));
    EscapeData.hAdapter              = pThis->mKmtCallbacks.hAdapter;
    EscapeData.hDevice               = pThis->mKmtCallbacks.hDevice;
    EscapeData.Type                  = D3DKMT_ESCAPE_DRIVERPRIVATE;
    EscapeData.Flags.HardwareAccess  = 1;
    EscapeData.pPrivateDriverData    = pData;
    EscapeData.PrivateDriverDataSize = cbAlloc;
    // EscapeData.hContext              = 0;

    NTSTATUS Status = pThis->mKmtCallbacks.d3dkmt->pfnD3DKMTEscape(&EscapeData);
    if (Status == STATUS_SUCCESS)
    {
        /* Create a kernel mode allocation for render targets,
         * because we will need kernel mode handles for Present.
         */
        if (pCreateParms->flags & SVGA3D_SURFACE_HINT_RENDERTARGET)
        {
            /* First check if the format is supported. */
            D3DDDIFORMAT const ddiFormat = svgaToD3DDDIFormat((SVGA3dSurfaceFormat)pCreateParms->format);
            if (ddiFormat != D3DDDIFMT_UNKNOWN)
            {
                GAWDDMSURFACEINFO *pSurfaceInfo = (GAWDDMSURFACEINFO *)malloc(sizeof(GAWDDMSURFACEINFO));
                if (pSurfaceInfo)
                {
                    memset(pSurfaceInfo, 0, sizeof(GAWDDMSURFACEINFO));

                    VBOXWDDM_ALLOCINFO wddmAllocInfo;
                    memset(&wddmAllocInfo, 0, sizeof(wddmAllocInfo));

                    wddmAllocInfo.enmType             = VBOXWDDM_ALLOC_TYPE_UMD_RC_GENERIC;
                    wddmAllocInfo.fFlags.RenderTarget = 1;
                    wddmAllocInfo.hSharedHandle       = 0;
                    wddmAllocInfo.hostID              = pData->u32Sid;
                    wddmAllocInfo.SurfDesc.slicePitch = 0;
                    wddmAllocInfo.SurfDesc.depth      = paSizes[0].cDepth;
                    wddmAllocInfo.SurfDesc.width      = paSizes[0].cWidth;
                    wddmAllocInfo.SurfDesc.height     = paSizes[0].cHeight;
                    wddmAllocInfo.SurfDesc.format     = ddiFormat;
                    wddmAllocInfo.SurfDesc.VidPnSourceId = 0;
                    wddmAllocInfo.SurfDesc.bpp        = vboxWddmCalcBitsPerPixel(wddmAllocInfo.SurfDesc.format);
                    wddmAllocInfo.SurfDesc.pitch      = vboxWddmCalcPitch(wddmAllocInfo.SurfDesc.width,
                                                                          wddmAllocInfo.SurfDesc.format);
                    wddmAllocInfo.SurfDesc.cbSize     = vboxWddmCalcSize(wddmAllocInfo.SurfDesc.pitch,
                                                                         wddmAllocInfo.SurfDesc.height,
                                                                         wddmAllocInfo.SurfDesc.format);
                    wddmAllocInfo.SurfDesc.d3dWidth   = vboxWddmCalcWidthForPitch(wddmAllocInfo.SurfDesc.pitch,
                                                                                  wddmAllocInfo.SurfDesc.format);

                    D3DDDI_ALLOCATIONINFO AllocationInfo;
                    memset(&AllocationInfo, 0, sizeof(AllocationInfo));
                    // AllocationInfo.hAllocation           = NULL;
                    // AllocationInfo.pSystemMem            = NULL;
                    AllocationInfo.pPrivateDriverData    = &wddmAllocInfo;
                    AllocationInfo.PrivateDriverDataSize = sizeof(wddmAllocInfo);

                    D3DKMT_CREATEALLOCATION CreateAllocation;
                    memset(&CreateAllocation, 0, sizeof(CreateAllocation));
                    CreateAllocation.hDevice         = pThis->mKmtCallbacks.hDevice;
                    CreateAllocation.NumAllocations  = 1;
                    CreateAllocation.pAllocationInfo = &AllocationInfo;

                    Status = pThis->mKmtCallbacks.d3dkmt->pfnD3DKMTCreateAllocation(&CreateAllocation);
                    if (Status == STATUS_SUCCESS)
                    {
                        pSurfaceInfo->Core.Key    = pData->u32Sid;
                        pSurfaceInfo->hAllocation = AllocationInfo.hAllocation;
                        if (!RTAvlU32Insert(&pThis->mSurfaceTree, &pSurfaceInfo->Core))
                        {
                            Status = STATUS_NOT_SUPPORTED;
                        }
                    }

                    if (Status != STATUS_SUCCESS)
                    {
                        free(pSurfaceInfo);
                    }
                }
                else
                {
                    Status = STATUS_NOT_SUPPORTED;
                }
            }
            else
            {
                /* Unsupported render target format. */
                Status = STATUS_NOT_SUPPORTED;
            }
        }

        if (Status != STATUS_SUCCESS)
        {
            gaEnvSurfaceDestroy(pvEnv, pData->u32Sid);
        }
    }

    if (Status == STATUS_SUCCESS)
    {
        *pu32Sid = pData->u32Sid;
        free(pData);
        return 0;
    }

    Assert(0);
    free(pData);
    return -1;
}

/* static */ DECLCALLBACK(void)
GaDrvEnvKmt::gaEnvSurfaceDestroy(void *pvEnv,
                                 uint32_t u32Sid)
{
    GaDrvEnvKmt *pThis = (GaDrvEnvKmt *)pvEnv;

    VBOXDISPIFESCAPE_GASURFACEDESTROY data;
    memset(&data, 0, sizeof(data));
    data.EscapeHdr.escapeCode  = VBOXESC_GASURFACEDESTROY;
    // data.EscapeHdr.cmdSpecific = 0;
    data.u32Sid                = u32Sid;

    D3DKMT_ESCAPE EscapeData;
    memset(&EscapeData, 0, sizeof(EscapeData));
    EscapeData.hAdapter              = pThis->mKmtCallbacks.hAdapter;
    EscapeData.hDevice               = pThis->mKmtCallbacks.hDevice;
    EscapeData.Type                  = D3DKMT_ESCAPE_DRIVERPRIVATE;
    EscapeData.Flags.HardwareAccess  = 1;
    EscapeData.pPrivateDriverData    = &data;
    EscapeData.PrivateDriverDataSize = sizeof(data);
    // EscapeData.hContext              = 0;

    NTSTATUS Status = pThis->mKmtCallbacks.d3dkmt->pfnD3DKMTEscape(&EscapeData);
    Assert(Status == STATUS_SUCCESS);

    /* Try to remove from sid -> hAllocation map. */
    GAWDDMSURFACEINFO *pSurfaceInfo = (GAWDDMSURFACEINFO *)RTAvlU32Remove(&pThis->mSurfaceTree, u32Sid);
    if (pSurfaceInfo)
    {
        D3DKMT_DESTROYALLOCATION DestroyAllocation;
        memset(&DestroyAllocation, 0, sizeof(DestroyAllocation));
        DestroyAllocation.hDevice          = pThis->mKmtCallbacks.hDevice;
        // DestroyAllocation.hResource     = 0;
        DestroyAllocation.phAllocationList = &pSurfaceInfo->hAllocation;
        DestroyAllocation.AllocationCount  = 1;

        Status = pThis->mKmtCallbacks.d3dkmt->pfnD3DKMTDestroyAllocation(&DestroyAllocation);
        Assert(Status == STATUS_SUCCESS);

        free(pSurfaceInfo);
    }
}

D3DKMT_HANDLE GaDrvEnvKmt::drvEnvKmtSurfaceHandle(uint32_t u32Sid)
{
    GAWDDMSURFACEINFO *pSurfaceInfo = (GAWDDMSURFACEINFO *)RTAvlU32Get(&mSurfaceTree, u32Sid);
    return pSurfaceInfo ? pSurfaceInfo->hAllocation : 0;
}

static bool
vboxDdiFenceCreate(GaKmtCallbacks *pKmtCallbacks,
                   GAWDDMCONTEXTINFO *pContextInfo,
                   uint32_t *pu32FenceHandle)
{
    VBOXDISPIFESCAPE_GAFENCECREATE fenceCreate;
    memset(&fenceCreate, 0, sizeof(fenceCreate));
    fenceCreate.EscapeHdr.escapeCode  = VBOXESC_GAFENCECREATE;
    // fenceCreate.EscapeHdr.cmdSpecific = 0;

    /* If the user-mode display driver sets hContext to a non-NULL value, the driver must
     * have also set hDevice to a non-NULL value...
     */
    D3DKMT_ESCAPE EscapeData;
    memset(&EscapeData, 0, sizeof(EscapeData));
    EscapeData.hAdapter              = pKmtCallbacks->hAdapter;
    EscapeData.hDevice               = pKmtCallbacks->hDevice;
    EscapeData.Type                  = D3DKMT_ESCAPE_DRIVERPRIVATE;
    // EscapeData.Flags.HardwareAccess  = 0;
    EscapeData.pPrivateDriverData    = &fenceCreate;
    EscapeData.PrivateDriverDataSize = sizeof(fenceCreate);
    EscapeData.hContext              = pContextInfo->hContext;

    NTSTATUS Status = pKmtCallbacks->d3dkmt->pfnD3DKMTEscape(&EscapeData);
    if (Status == STATUS_SUCCESS)
    {
        *pu32FenceHandle = fenceCreate.u32FenceHandle;
        return true;
    }

    Assert(0);
    return false;
}

static bool
vboxDdiFenceQuery(GaKmtCallbacks *pKmtCallbacks,
                  uint32_t u32FenceHandle,
                  GAFENCEQUERY *pFenceQuery)
{
    VBOXDISPIFESCAPE_GAFENCEQUERY fenceQuery;
    memset(&fenceQuery, 0, sizeof(fenceQuery));
    fenceQuery.EscapeHdr.escapeCode  = VBOXESC_GAFENCEQUERY;
    // fenceQuery.EscapeHdr.cmdSpecific = 0;
    fenceQuery.u32FenceHandle = u32FenceHandle;

    D3DKMT_ESCAPE EscapeData;
    memset(&EscapeData, 0, sizeof(EscapeData));
    EscapeData.hAdapter              = pKmtCallbacks->hAdapter;
    EscapeData.hDevice               = pKmtCallbacks->hDevice;
    EscapeData.Type                  = D3DKMT_ESCAPE_DRIVERPRIVATE;
    // EscapeData.Flags.HardwareAccess  = 0;
    EscapeData.pPrivateDriverData    = &fenceQuery;
    EscapeData.PrivateDriverDataSize = sizeof(fenceQuery);
    EscapeData.hContext              = 0;

    NTSTATUS Status = pKmtCallbacks->d3dkmt->pfnD3DKMTEscape(&EscapeData);
    if (Status == STATUS_SUCCESS)
    {
        pFenceQuery->u32FenceHandle    = fenceQuery.u32FenceHandle;
        pFenceQuery->u32SubmittedSeqNo = fenceQuery.u32SubmittedSeqNo;
        pFenceQuery->u32ProcessedSeqNo = fenceQuery.u32ProcessedSeqNo;
        pFenceQuery->u32FenceStatus    = fenceQuery.u32FenceStatus;
        return true;
    }

    Assert(0);
    return false;
}

/* static */ DECLCALLBACK(int)
GaDrvEnvKmt::gaEnvFenceQuery(void *pvEnv,
                             uint32_t u32FenceHandle,
                             GAFENCEQUERY *pFenceQuery)
{
    GaDrvEnvKmt *pThis = (GaDrvEnvKmt *)pvEnv;

    if (!pThis->mKmtCallbacks.hDevice)
    {
        pFenceQuery->u32FenceStatus = GA_FENCE_STATUS_NULL;
        return 0;
    }

    bool fSuccess = vboxDdiFenceQuery(&pThis->mKmtCallbacks, u32FenceHandle, pFenceQuery);
    return fSuccess ? 0: -1;
}

static bool
vboxDdiFenceWait(GaKmtCallbacks *pKmtCallbacks,
                 uint32_t u32FenceHandle,
                 uint32_t u32TimeoutUS)
{
    VBOXDISPIFESCAPE_GAFENCEWAIT fenceWait;
    memset(&fenceWait, 0, sizeof(fenceWait));
    fenceWait.EscapeHdr.escapeCode  = VBOXESC_GAFENCEWAIT;
    // pFenceWait->EscapeHdr.cmdSpecific = 0;
    fenceWait.u32FenceHandle = u32FenceHandle;
    fenceWait.u32TimeoutUS = u32TimeoutUS;

    D3DKMT_ESCAPE EscapeData;
    memset(&EscapeData, 0, sizeof(EscapeData));
    EscapeData.hAdapter              = pKmtCallbacks->hAdapter;
    EscapeData.hDevice               = pKmtCallbacks->hDevice;
    EscapeData.Type                  = D3DKMT_ESCAPE_DRIVERPRIVATE;
    // EscapeData.Flags.HardwareAccess  = 0;
    EscapeData.pPrivateDriverData    = &fenceWait;
    EscapeData.PrivateDriverDataSize = sizeof(fenceWait);
    EscapeData.hContext              = 0;

    NTSTATUS Status = pKmtCallbacks->d3dkmt->pfnD3DKMTEscape(&EscapeData);
    Assert(Status == STATUS_SUCCESS);
    return Status == STATUS_SUCCESS;
}

/* static */ DECLCALLBACK(int)
GaDrvEnvKmt::gaEnvFenceWait(void *pvEnv,
                                 uint32_t u32FenceHandle,
                                 uint32_t u32TimeoutUS)
{
    GaDrvEnvKmt *pThis = (GaDrvEnvKmt *)pvEnv;

    if (!pThis->mKmtCallbacks.hDevice)
        return 0;

    bool fSuccess = vboxDdiFenceWait(&pThis->mKmtCallbacks, u32FenceHandle, u32TimeoutUS);
    return fSuccess ? 0 : -1;
}

static bool
vboxDdiFenceUnref(GaKmtCallbacks *pKmtCallbacks,
                  uint32_t u32FenceHandle)
{
    VBOXDISPIFESCAPE_GAFENCEUNREF fenceUnref;
    memset(&fenceUnref, 0, sizeof(fenceUnref));
    fenceUnref.EscapeHdr.escapeCode  = VBOXESC_GAFENCEUNREF;
    // pFenceUnref->EscapeHdr.cmdSpecific = 0;
    fenceUnref.u32FenceHandle = u32FenceHandle;

    D3DKMT_ESCAPE EscapeData;
    memset(&EscapeData, 0, sizeof(EscapeData));
    EscapeData.hAdapter              = pKmtCallbacks->hAdapter;
    EscapeData.hDevice               = pKmtCallbacks->hDevice;
    EscapeData.Type                  = D3DKMT_ESCAPE_DRIVERPRIVATE;
    // EscapeData.Flags.HardwareAccess  = 0;
    EscapeData.pPrivateDriverData    = &fenceUnref;
    EscapeData.PrivateDriverDataSize = sizeof(fenceUnref);
    EscapeData.hContext              = 0;

    NTSTATUS Status = pKmtCallbacks->d3dkmt->pfnD3DKMTEscape(&EscapeData);
    Assert(Status == STATUS_SUCCESS);
    return Status == STATUS_SUCCESS;
}

/* static */ DECLCALLBACK(void)
GaDrvEnvKmt::gaEnvFenceUnref(void *pvEnv,
                                  uint32_t u32FenceHandle)
{
    GaDrvEnvKmt *pThis = (GaDrvEnvKmt *)pvEnv;

    if (!pThis->mKmtCallbacks.hDevice)
        return;

    vboxDdiFenceUnref(&pThis->mKmtCallbacks, u32FenceHandle);
}

/** Calculate how many commands will fit in the buffer.
 *
 * @param pu8Commands Command buffer.
 * @param cbCommands  Size of command buffer.
 * @param cbAvail     Available buffer size..
 * @param pu32Length  Size of commands which will fit in cbAvail bytes.
 */
static bool
vboxCalcCommandLength(const uint8_t *pu8Commands, uint32_t cbCommands, uint32_t cbAvail, uint32_t *pu32Length)
{
    uint32_t u32Length = 0;
    const uint8_t *pu8Src = pu8Commands;
    const uint8_t *pu8SrcEnd = pu8Commands + cbCommands;

    while (pu8SrcEnd > pu8Src)
    {
        const uint32_t cbSrcLeft = pu8SrcEnd - pu8Src;
        if (cbSrcLeft < sizeof(uint32_t))
        {
            return false;
        }

        /* Get the command id and command length. */
        const uint32_t u32CmdId = *(uint32_t *)pu8Src;
        uint32_t cbCmd = 0;

        if (SVGA_3D_CMD_BASE <= u32CmdId && u32CmdId < SVGA_3D_CMD_MAX)
        {
            if (cbSrcLeft < sizeof(SVGA3dCmdHeader))
            {
                return false;
            }

            const SVGA3dCmdHeader *pHeader = (SVGA3dCmdHeader *)pu8Src;
            cbCmd = sizeof(SVGA3dCmdHeader) + pHeader->size;
            if (cbCmd % sizeof(uint32_t) != 0)
            {
                return false;
            }
            if (cbSrcLeft < cbCmd)
            {
                return false;
            }
        }
        else
        {
            /* It is not expected that any of common SVGA commands will be in the command buffer
             * because the SVGA gallium driver does not use them.
             */
            return false;
        }

        if (u32Length + cbCmd > cbAvail)
        {
            if (u32Length == 0)
            {
               /* No commands fit into the buffer. */
               return false;
            }
            break;
        }

        pu8Src += cbCmd;
        u32Length += cbCmd;
    }

    *pu32Length = u32Length;
    return true;
}

static bool
vboxDdiRender(GaKmtCallbacks *pKmtCallbacks,
              GAWDDMCONTEXTINFO *pContextInfo, uint32_t u32FenceHandle, void *pvCommands, uint32_t cbCommands,
              ULONGLONG PresentHistoryToken, bool fPresentRedirected)
{
    uint32_t cbLeft;
    const uint8_t *pu8Src;

    cbLeft = cbCommands;
    pu8Src = (uint8_t *)pvCommands;
    /* Even when cbCommands is 0, submit the fence. The following code deals with this. */
    do
    {
        /* Actually available space. */
        const uint32_t cbAvail = pContextInfo->CommandBufferSize;
        if (cbAvail <= sizeof(u32FenceHandle))
        {
            return false;
        }

        /* How many bytes of command data still to copy. */
        uint32_t cbCommandChunk = cbLeft;

        /* How many bytes still to copy. */
        uint32_t cbToCopy = sizeof(u32FenceHandle) + cbCommandChunk;

        /* Copy the buffer identifier. */
        if (cbToCopy <= cbAvail)
        {
            /* Command buffer is big enough. */
            *(uint32_t *)pContextInfo->pCommandBuffer = u32FenceHandle;
        }
        else
        {
            /* Split. Write zero as buffer identifier. */
            *(uint32_t *)pContextInfo->pCommandBuffer = 0;

            /* Get how much commands data will fit in the buffer. */
            if (!vboxCalcCommandLength(pu8Src, cbCommandChunk, cbAvail - sizeof(u32FenceHandle), &cbCommandChunk))
            {
                return false;
            }

            cbToCopy = sizeof(u32FenceHandle) + cbCommandChunk;
        }

        if (cbCommandChunk)
        {
            /* Copy the command data. */
            memcpy((uint8_t *)pContextInfo->pCommandBuffer + sizeof(u32FenceHandle), pu8Src, cbCommandChunk);
        }

        /* Advance the command position. */
        pu8Src += cbCommandChunk;
        cbLeft -= cbCommandChunk;

        D3DKMT_RENDER RenderData;
        memset(&RenderData, 0, sizeof(RenderData));
        RenderData.hContext                = pContextInfo->hContext;
        // RenderData.CommandOffset           = 0;
        RenderData.CommandLength           = cbToCopy;
        // RenderData.AllocationCount         = 0;
        // RenderData.PatchLocationCount      = 0;
        RenderData.PresentHistoryToken     = PresentHistoryToken;
        RenderData.Flags.PresentRedirected = fPresentRedirected;

        NTSTATUS Status = pKmtCallbacks->d3dkmt->pfnD3DKMTRender(&RenderData);
        Assert(Status == STATUS_SUCCESS);
        if (Status != STATUS_SUCCESS)
        {
            return false;
        }

        pContextInfo->pCommandBuffer        = RenderData.pNewCommandBuffer;
        pContextInfo->CommandBufferSize     = RenderData.NewCommandBufferSize;
        pContextInfo->pAllocationList       = RenderData.pNewAllocationList;
        pContextInfo->AllocationListSize    = RenderData.NewAllocationListSize;
        pContextInfo->pPatchLocationList    = RenderData.pNewPatchLocationList;
        pContextInfo->PatchLocationListSize = RenderData.NewPatchLocationListSize;
    } while (cbLeft);

    return true;
}

bool GaDrvEnvKmt::doRender(uint32_t u32Cid, void *pvCommands, uint32_t cbCommands,
                          GAFENCEQUERY *pFenceQuery, ULONGLONG PresentHistoryToken, bool fPresentRedirected)
{
    uint32_t u32FenceHandle;
    GAWDDMCONTEXTINFO *pContextInfo = (GAWDDMCONTEXTINFO *)RTAvlU32Get(&mContextTree, u32Cid);
    if (!pContextInfo)
        return false;

    bool fSuccess = true;
    u32FenceHandle = 0;
    if (pFenceQuery)
    {
        fSuccess = vboxDdiFenceCreate(&mKmtCallbacks, pContextInfo, &u32FenceHandle);
    }

    if (fSuccess)
    {
        fSuccess = vboxDdiRender(&mKmtCallbacks, pContextInfo, u32FenceHandle,
                                 pvCommands, cbCommands, PresentHistoryToken, fPresentRedirected);
        if (fSuccess)
        {
            if (pFenceQuery)
            {
                if (!vboxDdiFenceQuery(&mKmtCallbacks, u32FenceHandle, pFenceQuery))
                {
                    pFenceQuery->u32FenceStatus = GA_FENCE_STATUS_NULL;
                }
            }
        }
    }
    return fSuccess;
}

/* static */ DECLCALLBACK(int)
GaDrvEnvKmt::gaEnvRender(void *pvEnv,
                         uint32_t u32Cid,
                         void *pvCommands,
                         uint32_t cbCommands,
                         GAFENCEQUERY *pFenceQuery)
{
    GaDrvEnvKmt *pThis = (GaDrvEnvKmt *)pvEnv;
    return pThis->doRender(u32Cid, pvCommands, cbCommands, pFenceQuery, 0, false) ? 1 : 0;
}

bool GaDrvEnvKmt::drvEnvKmtRenderCompose(uint32_t u32Cid,
                                         void *pvCommands,
                                         uint32_t cbCommands,
                                         ULONGLONG PresentHistoryToken)
{
    return doRender(u32Cid, pvCommands, cbCommands, NULL, PresentHistoryToken, true);
}


static bool
vboxDdiRegionCreate(GaKmtCallbacks *pKmtCallbacks,
                    uint32_t u32RegionSize,
                    uint32_t *pu32GmrId,
                    void **ppvMap)
{
    VBOXDISPIFESCAPE_GAREGION data;
    memset(&data, 0, sizeof(data));
    data.EscapeHdr.escapeCode  = VBOXESC_GAREGION;
    // data.EscapeHdr.cmdSpecific = 0;
    data.u32Command               = GA_REGION_CMD_CREATE;
    data.u32NumPages              = (u32RegionSize + PAGE_SIZE - 1) / PAGE_SIZE;
    // data.u32GmrId              = 0;
    // data.u64UserAddress        = 0;

    D3DKMT_ESCAPE EscapeData;
    memset(&EscapeData, 0, sizeof(EscapeData));
    EscapeData.hAdapter              = pKmtCallbacks->hAdapter;
    EscapeData.hDevice               = pKmtCallbacks->hDevice;
    EscapeData.Type                  = D3DKMT_ESCAPE_DRIVERPRIVATE;
    // EscapeData.Flags.HardwareAccess  = 0;
    EscapeData.pPrivateDriverData    = &data;
    EscapeData.PrivateDriverDataSize = sizeof(data);
    // EscapeData.hContext              = 0;

    NTSTATUS Status = pKmtCallbacks->d3dkmt->pfnD3DKMTEscape(&EscapeData);
    if (Status == STATUS_SUCCESS)
    {
        *pu32GmrId = data.u32GmrId;
        *ppvMap = (void *)(uintptr_t)data.u64UserAddress;
        return true;
    }

    Assert(0);
    return false;
}

/* static */ DECLCALLBACK(int)
GaDrvEnvKmt::gaEnvRegionCreate(void *pvEnv,
                               uint32_t u32RegionSize,
                               uint32_t *pu32GmrId,
                               void **ppvMap)
{
    GaDrvEnvKmt *pThis = (GaDrvEnvKmt *)pvEnv;

    if (pThis->mKmtCallbacks.hDevice)
    {
        /* That is a real device */
        bool fSuccess = vboxDdiRegionCreate(&pThis->mKmtCallbacks, u32RegionSize, pu32GmrId, ppvMap);
        return fSuccess ? 0: -1;
    }

    /* That is a fake device, created when WDDM adapter is initialized. */
    *ppvMap = malloc(u32RegionSize);
    if (*ppvMap)
    {
        *pu32GmrId = 0;
        return 0;
    }

    return -1;
}

static bool
vboxDdiRegionDestroy(GaKmtCallbacks *pKmtCallbacks,
                     uint32_t u32GmrId)
{
    VBOXDISPIFESCAPE_GAREGION data;
    memset(&data, 0, sizeof(data));
    data.EscapeHdr.escapeCode  = VBOXESC_GAREGION;
    // data.EscapeHdr.cmdSpecific = 0;
    data.u32Command               = GA_REGION_CMD_DESTROY;
    // data.u32NumPages           = 0;
    data.u32GmrId                 = u32GmrId;
    // data.u64UserAddress        = 0;

    D3DKMT_ESCAPE EscapeData;
    memset(&EscapeData, 0, sizeof(EscapeData));
    EscapeData.hAdapter              = pKmtCallbacks->hAdapter;
    EscapeData.hDevice               = pKmtCallbacks->hDevice;
    EscapeData.Type                  = D3DKMT_ESCAPE_DRIVERPRIVATE;
    EscapeData.Flags.HardwareAccess  = 1; /* Sync with submitted commands. */
    EscapeData.pPrivateDriverData    = &data;
    EscapeData.PrivateDriverDataSize = sizeof(data);
    // EscapeData.hContext              = 0;

    NTSTATUS Status = pKmtCallbacks->d3dkmt->pfnD3DKMTEscape(&EscapeData);
    Assert(Status == STATUS_SUCCESS);
    return Status == STATUS_SUCCESS;
}

/* static */ DECLCALLBACK(void)
GaDrvEnvKmt::gaEnvRegionDestroy(void *pvEnv,
                                     uint32_t u32GmrId,
                                     void *pvMap)
{
    GaDrvEnvKmt *pThis = (GaDrvEnvKmt *)pvEnv;

    if (pThis->mKmtCallbacks.hDevice)
    {
        vboxDdiRegionDestroy(&pThis->mKmtCallbacks, u32GmrId);
    }
    else
    {
        free(pvMap);
    }
}

/* static */ DECLCALLBACK(int)
GaDrvEnvKmt::gaEnvGBSurfaceDefine(void *pvEnv,
                                  SVGAGBSURFCREATE *pCreateParms)
{
    GaDrvEnvKmt *pThis = (GaDrvEnvKmt *)pvEnv;

    VBOXDISPIFESCAPE_SVGAGBSURFACEDEFINE data;
    data.EscapeHdr.escapeCode     = VBOXESC_SVGAGBSURFACEDEFINE;
    data.EscapeHdr.u32CmdSpecific = 0;
    data.CreateParms              = *pCreateParms;

    D3DKMT_ESCAPE EscapeData;
    memset(&EscapeData, 0, sizeof(EscapeData));
    EscapeData.hAdapter              = pThis->mKmtCallbacks.hAdapter;
    EscapeData.hDevice               = pThis->mKmtCallbacks.hDevice;
    EscapeData.Type                  = D3DKMT_ESCAPE_DRIVERPRIVATE;
    EscapeData.Flags.HardwareAccess  = 1;
    EscapeData.pPrivateDriverData    = &data;
    EscapeData.PrivateDriverDataSize = sizeof(data);
    // EscapeData.hContext              = 0;

    NTSTATUS Status = pThis->mKmtCallbacks.d3dkmt->pfnD3DKMTEscape(&EscapeData);
    if (Status == STATUS_SUCCESS)
    {
        pCreateParms->gmrid = data.CreateParms.gmrid;
        pCreateParms->cbGB = data.CreateParms.cbGB;
        pCreateParms->u64UserAddress = data.CreateParms.u64UserAddress;
        pCreateParms->u32Sid = data.CreateParms.u32Sid;

        /* Create a kernel mode allocation for render targets,
         * because we will need kernel mode handles for Present.
         */
        if (pCreateParms->s.flags & SVGA3D_SURFACE_HINT_RENDERTARGET)
        {
            /* First check if the format is supported. */
            D3DDDIFORMAT const ddiFormat = svgaToD3DDDIFormat((SVGA3dSurfaceFormat)pCreateParms->s.format);
            if (ddiFormat != D3DDDIFMT_UNKNOWN)
            {
                GAWDDMSURFACEINFO *pSurfaceInfo = (GAWDDMSURFACEINFO *)malloc(sizeof(GAWDDMSURFACEINFO));
                if (pSurfaceInfo)
                {
                    memset(pSurfaceInfo, 0, sizeof(GAWDDMSURFACEINFO));

                    VBOXWDDM_ALLOCINFO wddmAllocInfo;
                    memset(&wddmAllocInfo, 0, sizeof(wddmAllocInfo));

                    wddmAllocInfo.enmType             = VBOXWDDM_ALLOC_TYPE_UMD_RC_GENERIC;
                    wddmAllocInfo.fFlags.RenderTarget = 1;
                    wddmAllocInfo.hSharedHandle       = 0;
                    wddmAllocInfo.hostID              = pCreateParms->u32Sid;
                    wddmAllocInfo.SurfDesc.slicePitch = 0;
                    wddmAllocInfo.SurfDesc.depth      = pCreateParms->s.size.depth;
                    wddmAllocInfo.SurfDesc.width      = pCreateParms->s.size.width;
                    wddmAllocInfo.SurfDesc.height     = pCreateParms->s.size.height;
                    wddmAllocInfo.SurfDesc.format     = ddiFormat;
                    wddmAllocInfo.SurfDesc.VidPnSourceId = 0;
                    wddmAllocInfo.SurfDesc.bpp        = vboxWddmCalcBitsPerPixel(wddmAllocInfo.SurfDesc.format);
                    wddmAllocInfo.SurfDesc.pitch      = vboxWddmCalcPitch(wddmAllocInfo.SurfDesc.width,
                                                                          wddmAllocInfo.SurfDesc.format);
                    wddmAllocInfo.SurfDesc.cbSize     = vboxWddmCalcSize(wddmAllocInfo.SurfDesc.pitch,
                                                                         wddmAllocInfo.SurfDesc.height,
                                                                         wddmAllocInfo.SurfDesc.format);
                    wddmAllocInfo.SurfDesc.d3dWidth   = vboxWddmCalcWidthForPitch(wddmAllocInfo.SurfDesc.pitch,
                                                                                  wddmAllocInfo.SurfDesc.format);

                    D3DDDI_ALLOCATIONINFO AllocationInfo;
                    memset(&AllocationInfo, 0, sizeof(AllocationInfo));
                    // AllocationInfo.hAllocation           = NULL;
                    // AllocationInfo.pSystemMem            = NULL;
                    AllocationInfo.pPrivateDriverData    = &wddmAllocInfo;
                    AllocationInfo.PrivateDriverDataSize = sizeof(wddmAllocInfo);

                    D3DKMT_CREATEALLOCATION CreateAllocation;
                    memset(&CreateAllocation, 0, sizeof(CreateAllocation));
                    CreateAllocation.hDevice         = pThis->mKmtCallbacks.hDevice;
                    CreateAllocation.NumAllocations  = 1;
                    CreateAllocation.pAllocationInfo = &AllocationInfo;

                    Status = pThis->mKmtCallbacks.d3dkmt->pfnD3DKMTCreateAllocation(&CreateAllocation);
                    if (Status == STATUS_SUCCESS)
                    {
                        pSurfaceInfo->Core.Key    = pCreateParms->u32Sid;
                        pSurfaceInfo->hAllocation = AllocationInfo.hAllocation;
                        if (!RTAvlU32Insert(&pThis->mSurfaceTree, &pSurfaceInfo->Core))
                        {
                            Status = STATUS_NOT_SUPPORTED;
                        }
                    }

                    if (Status != STATUS_SUCCESS)
                    {
                        free(pSurfaceInfo);
                    }
                }
                else
                {
                    Status = STATUS_NOT_SUPPORTED;
                }
            }
            else
            {
                /* Unsupported render target format. */
                Assert(0);
                Status = STATUS_NOT_SUPPORTED;
            }
        }

        if (Status != STATUS_SUCCESS)
        {
            gaEnvSurfaceDestroy(pvEnv, pCreateParms->u32Sid);
        }
    }

    if (Status == STATUS_SUCCESS)
        return 0;

    Assert(0);
    return -1;
}

GaDrvEnvKmt::GaDrvEnvKmt()
    :
    mContextTree(0),
    mSurfaceTree(0)
{
    RT_ZERO(mKmtCallbacks);
    RT_ZERO(mHWInfo);
    RT_ZERO(mEnv);
}

GaDrvEnvKmt::~GaDrvEnvKmt()
{
}

HRESULT GaDrvEnvKmt::Init(void)
{
    mKmtCallbacks.d3dkmt = D3DKMTFunctions();

    /* Figure out which adapter to use. */
    NTSTATUS Status = vboxDispKmtOpenAdapter2(&mKmtCallbacks.hAdapter, &mKmtCallbacks.AdapterLuid);
    Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        VBOXWDDM_QAI adapterInfo;
        bool fSuccess = vboxDdiQueryAdapterInfo(&mKmtCallbacks, mKmtCallbacks.hAdapter, &adapterInfo, sizeof(adapterInfo));
        Assert(fSuccess);
        if (fSuccess)
        {
            fSuccess = vboxDdiDeviceCreate(&mKmtCallbacks, &mKmtCallbacks.hDevice);
            Assert(fSuccess);
            if (fSuccess)
            {
                mHWInfo = adapterInfo.u.vmsvga.HWInfo;

                /*
                 * Success.
                 */
                return S_OK;
            }

            vboxDdiDeviceDestroy(&mKmtCallbacks, mKmtCallbacks.hDevice);
        }

        vboxDispKmtCloseAdapter(mKmtCallbacks.hAdapter);
    }

    return E_FAIL;
}

const WDDMGalliumDriverEnv *GaDrvEnvKmt::Env()
{
    if (mEnv.cb == 0)
    {
        mEnv.cb                = sizeof(WDDMGalliumDriverEnv);
        mEnv.pHWInfo           = &mHWInfo;
        mEnv.pvEnv             = this;
        mEnv.pfnContextCreate  = gaEnvContextCreate;
        mEnv.pfnContextDestroy = gaEnvContextDestroy;
        mEnv.pfnSurfaceDefine  = gaEnvSurfaceDefine;
        mEnv.pfnSurfaceDestroy = gaEnvSurfaceDestroy;
        mEnv.pfnRender         = gaEnvRender;
        mEnv.pfnFenceUnref     = gaEnvFenceUnref;
        mEnv.pfnFenceQuery     = gaEnvFenceQuery;
        mEnv.pfnFenceWait      = gaEnvFenceWait;
        mEnv.pfnRegionCreate   = gaEnvRegionCreate;
        mEnv.pfnRegionDestroy  = gaEnvRegionDestroy;
        /* VGPU10 */
        mEnv.pfnGBSurfaceDefine  = gaEnvGBSurfaceDefine;
    }

    return &mEnv;
}

RT_C_DECLS_BEGIN

const WDDMGalliumDriverEnv *GaDrvEnvKmtCreate(void)
{
    GaDrvEnvKmt *p = new GaDrvEnvKmt();
    if (p)
    {
        HRESULT hr = p->Init();
        if (hr != S_OK)
        {
            delete p;
            p = NULL;
        }
    }
    return p ? p->Env() : NULL;
}

void GaDrvEnvKmtDelete(const WDDMGalliumDriverEnv *pEnv)
{
    if (pEnv)
    {
        GaDrvEnvKmt *p = (GaDrvEnvKmt *)pEnv->pvEnv;
        delete p;
    }
}

D3DKMT_HANDLE GaDrvEnvKmtContextHandle(const WDDMGalliumDriverEnv *pEnv, uint32_t u32Cid)
{
    GaDrvEnvKmt *p = (GaDrvEnvKmt *)pEnv->pvEnv;
    return p->drvEnvKmtContextHandle(u32Cid);
}

D3DKMT_HANDLE GaDrvEnvKmtSurfaceHandle(const WDDMGalliumDriverEnv *pEnv, uint32_t u32Sid)
{
    GaDrvEnvKmt *p = (GaDrvEnvKmt *)pEnv->pvEnv;
    return p->drvEnvKmtSurfaceHandle(u32Sid);
}

void GaDrvEnvKmtAdapterLUID(const WDDMGalliumDriverEnv *pEnv, LUID *pAdapterLuid)
{
    GaDrvEnvKmt *p = (GaDrvEnvKmt *)pEnv->pvEnv;
    *pAdapterLuid = p->mKmtCallbacks.AdapterLuid;
}

D3DKMT_HANDLE GaDrvEnvKmtAdapterHandle(const WDDMGalliumDriverEnv *pEnv)
{
    GaDrvEnvKmt *p = (GaDrvEnvKmt *)pEnv->pvEnv;
    return p->mKmtCallbacks.hAdapter;
}

D3DKMT_HANDLE GaDrvEnvKmtDeviceHandle(const WDDMGalliumDriverEnv *pEnv)
{
    GaDrvEnvKmt *p = (GaDrvEnvKmt *)pEnv->pvEnv;
    return p->mKmtCallbacks.hDevice;
}

void GaDrvEnvKmtRenderCompose(const WDDMGalliumDriverEnv *pEnv,
                              uint32_t u32Cid,
                              void *pvCommands,
                              uint32_t cbCommands,
                              ULONGLONG PresentHistoryToken)
{
    GaDrvEnvKmt *p = (GaDrvEnvKmt *)pEnv->pvEnv;
    p->drvEnvKmtRenderCompose(u32Cid, pvCommands, cbCommands, PresentHistoryToken);
}

RT_C_DECLS_END
