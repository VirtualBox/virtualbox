/* $Id: DevVGA-SVGA3d.cpp 110140 2025-07-07 18:00:16Z vitali.pelenjow@oracle.com $ */
/** @file
 * DevSVGA3d - VMWare SVGA device, 3D parts - Common core code.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#include <VBox/vmm/pdmdev.h>
#include <iprt/errcore.h>
#include <VBox/log.h>

#include <iprt/assert.h>
#include <iprt/mem.h>

#include <VBox/vmm/pgm.h> /* required by DevVGA.h */
#include <VBoxVideo.h> /* required by DevVGA.h */

/* should go BEFORE any other DevVGA include to make all DevVGA.h config defines be visible */
#include "DevVGA.h"

#include "DevVGA-SVGA.h"
#include "DevVGA-SVGA3d.h"
#define VMSVGA3D_INCL_STRUCTURE_DESCRIPTORS
#include "DevVGA-SVGA3d-internal.h"
#include "DevVGA-SVGA-internal.h"


static int vmsvga3dSurfaceAllocMipLevels(PVMSVGA3DSURFACE pSurface)
{
    /* Allocate buffer to hold the surface data until we can move it into a D3D object */
    for (uint32_t i = 0; i < pSurface->cLevels * pSurface->surfaceDesc.numArrayElements; ++i)
    {
        PVMSVGA3DMIPMAPLEVEL pMipmapLevel = &pSurface->paMipmapLevels[i];
        AssertReturn(pMipmapLevel->pSurfaceData == NULL, VERR_INVALID_STATE);
        pMipmapLevel->pSurfaceData = RTMemAllocZ(pMipmapLevel->cbSurface);
        AssertReturn(pMipmapLevel->pSurfaceData, VERR_NO_MEMORY);
    }
    return VINF_SUCCESS;
}


static void vmsvga3dSurfaceFreeMipLevels(PVMSVGA3DSURFACE pSurface)
{
    for (uint32_t i = 0; i < pSurface->cLevels * pSurface->surfaceDesc.numArrayElements; ++i)
    {
        PVMSVGA3DMIPMAPLEVEL pMipmapLevel = &pSurface->paMipmapLevels[i];
        RTMemFreeZ(pMipmapLevel->pSurfaceData, pMipmapLevel->cbSurface);
        pMipmapLevel->pSurfaceData = NULL;
    }
}


/**
 * Implements the SVGA_3D_CMD_SURFACE_DEFINE_V2 and SVGA_3D_CMD_SURFACE_DEFINE
 * commands (fifo).
 *
 * @returns VBox status code (currently ignored).
 * @param   pThisCC             The VGA/VMSVGA state for ring-3.
 * @param   sid                 The ID of the surface to (re-)define.
 * @param   surfaceFlags        .
 * @param   format              .
 * @param   multisampleCount    .
 * @param   multisamplePattern  .
 * @param   qualityLevel        .
 * @param   autogenFilter       .
 * @param   numMipLevels        .
 * @param   pMipLevel0Size      .
 * @param   arraySize           Number of elements in a texture array.
 * @param   bufferByteStride    .
 * @param   fAllocMipLevels     .
 */
int vmsvga3dSurfaceDefine(PVGASTATECC pThisCC, uint32_t sid, SVGA3dSurfaceAllFlags surfaceFlags, SVGA3dSurfaceFormat format,
                          uint32_t multisampleCount, SVGA3dMSPattern multisamplePattern, SVGA3dMSQualityLevel qualityLevel, SVGA3dTextureFilter autogenFilter,
                          uint32_t numMipLevels, SVGA3dSize const *pMipLevel0Size, uint32_t arraySize, uint32_t bufferByteStride, bool fAllocMipLevels)
{
    PVMSVGA3DSURFACE pSurface;
    PVMSVGA3DSTATE   pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    LogFunc(("sid=%u surfaceFlags=0x%RX64 format=%s (%#x) multiSampleCount=%d autogenFilter=%d, numMipLevels=%d size=%dx%dx%d, arraySize=%u\n",
             sid, surfaceFlags, vmsvgaLookupEnum((int)format, &g_SVGA3dSurfaceFormat2String), format, multisampleCount, autogenFilter,
             numMipLevels, pMipLevel0Size->width, pMipLevel0Size->height, pMipLevel0Size->depth, arraySize));

    ASSERT_GUEST_RETURN(sid < SVGA3D_MAX_SURFACE_IDS, VERR_INVALID_PARAMETER);
    ASSERT_GUEST_RETURN(numMipLevels >= 1 && numMipLevels <= SVGA3D_MAX_MIP_LEVELS, VERR_INVALID_PARAMETER);
    ASSERT_GUEST_RETURN(arraySize <= SVGA3D_MAX_SURFACE_ARRAYSIZE, VERR_INVALID_PARAMETER);

    if (sid >= pState->cSurfaces)
    {
        /* Grow the array. */
        uint32_t cNew = RT_ALIGN(sid + 15, 16);
        void *pvNew = RTMemRealloc(pState->papSurfaces, sizeof(pState->papSurfaces[0]) * cNew);
        AssertReturn(pvNew, VERR_NO_MEMORY);
        pState->papSurfaces = (PVMSVGA3DSURFACE *)pvNew;
        while (pState->cSurfaces < cNew)
        {
            pSurface = (PVMSVGA3DSURFACE)RTMemAllocZ(sizeof(*pSurface));
            AssertReturn(pSurface, VERR_NO_MEMORY);
            pSurface->id = SVGA3D_INVALID_ID;
            pState->papSurfaces[pState->cSurfaces++] = pSurface;
        }
    }
    pSurface = pState->papSurfaces[sid];

    /* If one already exists with this id, then destroy it now. */
    if (pSurface->id != SVGA3D_INVALID_ID)
        vmsvga3dSurfaceDestroy(pThisCC, sid);

    RT_ZERO(*pSurface);
    // pSurface->pBackendSurface    = NULL;
    pSurface->id                    = SVGA3D_INVALID_ID; /* Keep this value until the surface init completes */
    pSurface->idAssociatedContext   = SVGA3D_INVALID_ID;

    if (arraySize)
        pSurface->surfaceDesc.numArrayElements = arraySize; /* Also for an array of cubemaps where arraySize = 6 * numCubes. */
    else if (surfaceFlags & SVGA3D_SURFACE_CUBEMAP)
        pSurface->surfaceDesc.numArrayElements = SVGA3D_MAX_SURFACE_FACES;
    else
        pSurface->surfaceDesc.numArrayElements = 1;

    pSurface->surfaceDesc.multisampleCount = RT_MAX(multisampleCount, 1);
    pSurface->surfaceDesc.multisamplePattern = RT_CLAMP(multisamplePattern, SVGA3D_MS_PATTERN_MIN, SVGA3D_MS_PATTERN_MAX);
    pSurface->surfaceDesc.qualityLevel = RT_CLAMP(qualityLevel, SVGA3D_MS_QUALITY_MIN, SVGA3D_MS_QUALITY_MAX);
    pSurface->surfaceDesc.bufferByteStride = bufferByteStride;

    /** @todo This 'switch' and the surfaceFlags tweaks should not be necessary.
     * The actual surface type will be figured out when the surface is actually used later.
     * The backends code must be reviewed for unnecessary dependencies on the surfaceFlags value.
     */
    /* The surface type is sort of undefined now, even though the hints and format can help to clear that up.
     * In some case we'll have to wait until the surface is used to create the D3D object.
     */
    switch (format)
    {
    case SVGA3D_Z_D32:
    case SVGA3D_Z_D16:
    case SVGA3D_Z_D24S8:
    case SVGA3D_Z_D15S1:
    case SVGA3D_Z_D24X8:
    case SVGA3D_Z_DF16:
    case SVGA3D_Z_DF24:
    case SVGA3D_Z_D24S8_INT:
        Assert(surfaceFlags & SVGA3D_SURFACE_HINT_DEPTHSTENCIL);
        surfaceFlags |= SVGA3D_SURFACE_HINT_DEPTHSTENCIL;
        break;

    /* Texture compression formats */
    case SVGA3D_DXT1:
    case SVGA3D_DXT2:
    case SVGA3D_DXT3:
    case SVGA3D_DXT4:
    case SVGA3D_DXT5:
    /* Bump-map formats */
    case SVGA3D_BUMPU8V8:
    case SVGA3D_BUMPL6V5U5:
    case SVGA3D_BUMPX8L8V8U8:
    case SVGA3D_V8U8:
    case SVGA3D_Q8W8V8U8:
    case SVGA3D_CxV8U8:
    case SVGA3D_X8L8V8U8:
    case SVGA3D_A2W10V10U10:
    case SVGA3D_V16U16:
    /* Typical render target formats; we should allow render target buffers to be used as textures. */
    case SVGA3D_X8R8G8B8:
    case SVGA3D_A8R8G8B8:
    case SVGA3D_R5G6B5:
    case SVGA3D_X1R5G5B5:
    case SVGA3D_A1R5G5B5:
    case SVGA3D_A4R4G4B4:
        Assert(surfaceFlags & (SVGA3D_SURFACE_HINT_TEXTURE | SVGA3D_SURFACE_SCREENTARGET));
        surfaceFlags |= SVGA3D_SURFACE_HINT_TEXTURE;
        break;

    case SVGA3D_LUMINANCE8:
    case SVGA3D_LUMINANCE4_ALPHA4:
    case SVGA3D_LUMINANCE16:
    case SVGA3D_LUMINANCE8_ALPHA8:
    case SVGA3D_ARGB_S10E5:   /* 16-bit floating-point ARGB */
    case SVGA3D_ARGB_S23E8:   /* 32-bit floating-point ARGB */
    case SVGA3D_A2R10G10B10:
    case SVGA3D_ALPHA8:
    case SVGA3D_R_S10E5:
    case SVGA3D_R_S23E8:
    case SVGA3D_RG_S10E5:
    case SVGA3D_RG_S23E8:
    case SVGA3D_G16R16:
    case SVGA3D_A16B16G16R16:
    case SVGA3D_UYVY:
    case SVGA3D_YUY2:
    case SVGA3D_NV12:
    case SVGA3D_FORMAT_DEAD2: /* Old SVGA3D_AYUV */
    case SVGA3D_ATI1:
    case SVGA3D_ATI2:
        break;

    /*
     * Any surface can be used as a buffer object, but SVGA3D_BUFFER is
     * the most efficient format to use when creating new surfaces
     * expressly for index or vertex data.
     */
    case SVGA3D_BUFFER:
        break;

    default:
        break;
    }

    pSurface->f.surfaceFlags    = surfaceFlags;
    pSurface->format            = format;
    /* cFaces is 6 for a cubemaps and 1 otherwise. */
    pSurface->cFaces            = (uint32_t)((surfaceFlags & SVGA3D_SURFACE_CUBEMAP) ? 6 : 1);
    pSurface->cLevels           = numMipLevels;
    pSurface->multiSampleCount  = pSurface->surfaceDesc.multisampleCount; /** @todo Remove the field. */
    pSurface->autogenFilter     = autogenFilter;
    Assert(autogenFilter != SVGA3D_TEX_FILTER_FLATCUBIC);
    Assert(autogenFilter != SVGA3D_TEX_FILTER_GAUSSIANCUBIC);
    pSurface->paMipmapLevels    = (PVMSVGA3DMIPMAPLEVEL)RTMemAllocZ(numMipLevels * pSurface->surfaceDesc.numArrayElements * sizeof(VMSVGA3DMIPMAPLEVEL));
    AssertReturn(pSurface->paMipmapLevels, VERR_NO_MEMORY);

    pSurface->cbBlock = vmsvga3dSurfaceFormatSize(format, &pSurface->cxBlock, &pSurface->cyBlock, &pSurface->cbPitchBlock);
    AssertReturn(pSurface->cbBlock, VERR_INVALID_PARAMETER);

    /** @todo cbMemRemaining = value of SVGA_REG_MOB_MAX_SIZE */
    uint32_t cbMemRemaining = SVGA3D_MAX_SURFACE_MEM_SIZE; /* Do not allow more than this for a surface. */
    SVGA3dSize mipmapSize = *pMipLevel0Size;
    int rc = VINF_SUCCESS;

    for (uint32_t i = 0; i < numMipLevels; ++i)
    {
        for (uint32_t iArray = 0; iArray < pSurface->surfaceDesc.numArrayElements; ++iArray)
        {
            uint32_t const iMipmap = vmsvga3dCalcSubresource(i, iArray, numMipLevels);
            LogFunc(("[%d] array %d mip level %d (%d,%d,%d) cbBlock=%#x block %dx%d\n",
                     iMipmap, iArray, i, mipmapSize.width, mipmapSize.height, mipmapSize.depth,
                     pSurface->cbBlock, pSurface->cxBlock, pSurface->cyBlock));

            uint32_t cBlocksX;
            uint32_t cBlocksY;
            uint32_t cbSurfacePitch;
            uint32_t cbSurfacePlane;
            uint32_t cbSurface;
            vmsvga3dSurfaceMipBufferSize(format, mipmapSize, pSurface->surfaceDesc.multisampleCount,
                                         &cBlocksX, &cBlocksY, &cbSurfacePitch, &cbSurfacePlane, &cbSurface);
            AssertBreakStmt(cbMemRemaining >= cbSurface, rc = VERR_INVALID_PARAMETER);

            PVMSVGA3DMIPMAPLEVEL pMipmapLevel = &pSurface->paMipmapLevels[iMipmap];
            pMipmapLevel->mipmapSize     = mipmapSize;
            pMipmapLevel->cBlocksX       = cBlocksX;
            pMipmapLevel->cBlocksY       = cBlocksY;
            pMipmapLevel->cBlocks        = cBlocksX * cBlocksY * mipmapSize.depth;
            pMipmapLevel->cbSurfacePitch = cbSurfacePitch;
            pMipmapLevel->cbSurfacePlane = cbSurfacePlane;
            pMipmapLevel->cbSurface      = cbSurface;
            pMipmapLevel->pSurfaceData   = NULL;

            cbMemRemaining -= cbSurface;
        }

        AssertRCBreak(rc);

        mipmapSize.width >>= 1;
        if (mipmapSize.width == 0) mipmapSize.width = 1;
        mipmapSize.height >>= 1;
        if (mipmapSize.height == 0) mipmapSize.height = 1;
        mipmapSize.depth >>= 1;
        if (mipmapSize.depth == 0) mipmapSize.depth = 1;
    }

    AssertLogRelRCReturnStmt(rc, RTMemFree(pSurface->paMipmapLevels), rc);

    /* Compute the size of one array element. */
    pSurface->surfaceDesc.cbArrayElement = 0;
    for (uint32_t i = 0; i < pSurface->cLevels; ++i)
    {
        PVMSVGA3DMIPMAPLEVEL pMipLevel = &pSurface->paMipmapLevels[i];
        pSurface->surfaceDesc.cbArrayElement += pMipLevel->cbSurface;
    }

    if (vmsvga3dIsLegacyBackend(pThisCC))
    {
#ifdef VMSVGA3D_DIRECT3D
        /* pSurface->hSharedObject = NULL; */
        /* pSurface->pSharedObjectTree = NULL; */
        /* Translate the format and usage flags to D3D. */
        pSurface->d3dfmtRequested   = vmsvga3dSurfaceFormat2D3D(format);
        pSurface->formatD3D         = D3D9GetActualFormat(pState, pSurface->d3dfmtRequested);
        pSurface->multiSampleTypeD3D= vmsvga3dMultipeSampleCount2D3D(multisampleCount);
        pSurface->fUsageD3D         = 0;
        if (surfaceFlags & SVGA3D_SURFACE_HINT_DYNAMIC)
            pSurface->fUsageD3D |= D3DUSAGE_DYNAMIC;
        if (surfaceFlags & SVGA3D_SURFACE_HINT_RENDERTARGET)
            pSurface->fUsageD3D |= D3DUSAGE_RENDERTARGET;
        if (surfaceFlags & SVGA3D_SURFACE_HINT_DEPTHSTENCIL)
            pSurface->fUsageD3D |= D3DUSAGE_DEPTHSTENCIL;
        if (surfaceFlags & SVGA3D_SURFACE_HINT_WRITEONLY)
            pSurface->fUsageD3D |= D3DUSAGE_WRITEONLY;
        if (surfaceFlags & SVGA3D_SURFACE_AUTOGENMIPMAPS)
            pSurface->fUsageD3D |= D3DUSAGE_AUTOGENMIPMAP;
        pSurface->enmD3DResType = VMSVGA3D_D3DRESTYPE_NONE;
        /* pSurface->u.pSurface = NULL; */
        /* pSurface->bounce.pTexture = NULL; */
        /* pSurface->emulated.pTexture = NULL; */
#else
        /* pSurface->oglId.buffer = OPENGL_INVALID_ID; */
        /* pSurface->fEmulated = false; */
        /* pSurface->idEmulated = OPENGL_INVALID_ID; */
        vmsvga3dSurfaceFormat2OGL(pSurface, format);
#endif
    }

#ifdef LOG_ENABLED
    SVGA3dSurfaceAllFlags const f = surfaceFlags;
    LogFunc(("surface flags:%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s 0x%RX64\n",
            (f & SVGA3D_SURFACE_CUBEMAP)              ? " CUBEMAP"  : "",
            (f & SVGA3D_SURFACE_HINT_STATIC)          ? " HINT_STATIC"  : "",
            (f & SVGA3D_SURFACE_HINT_DYNAMIC)         ? " HINT_DYNAMIC"  : "",
            (f & SVGA3D_SURFACE_HINT_INDEXBUFFER)     ? " HINT_INDEXBUFFER"  : "",
            (f & SVGA3D_SURFACE_HINT_VERTEXBUFFER)    ? " HINT_VERTEXBUFFER"  : "",
            (f & SVGA3D_SURFACE_HINT_TEXTURE)         ? " HINT_TEXTURE"  : "",
            (f & SVGA3D_SURFACE_HINT_RENDERTARGET)    ? " HINT_RENDERTARGET"  : "",
            (f & SVGA3D_SURFACE_HINT_DEPTHSTENCIL)    ? " HINT_DEPTHSTENCIL"  : "",
            (f & SVGA3D_SURFACE_HINT_WRITEONLY)       ? " HINT_WRITEONLY"  : "",
            (f & SVGA3D_SURFACE_DEAD2)                ? " DEAD2"  : "",
            (f & SVGA3D_SURFACE_AUTOGENMIPMAPS)       ? " AUTOGENMIPMAPS"  : "",
            (f & SVGA3D_SURFACE_DEAD1)                ? " DEAD1"  : "",
            (f & SVGA3D_SURFACE_MOB_PITCH)            ? " MOB_PITCH"  : "",
            (f & SVGA3D_SURFACE_INACTIVE)             ? " INACTIVE"  : "",
            (f & SVGA3D_SURFACE_HINT_RT_LOCKABLE)     ? " HINT_RT_LOCKABLE"  : "",
            (f & SVGA3D_SURFACE_VOLUME)               ? " VOLUME"  : "",
            (f & SVGA3D_SURFACE_SCREENTARGET)         ? " SCREENTARGET"  : "",
            (f & SVGA3D_SURFACE_ALIGN16)              ? " ALIGN16"  : "",
            (f & SVGA3D_SURFACE_1D)                   ? " 1D"  : "",
            (f & SVGA3D_SURFACE_ARRAY)                ? " ARRAY"  : "",
            (f & SVGA3D_SURFACE_BIND_VERTEX_BUFFER)   ? " BIND_VERTEX_BUFFER"  : "",
            (f & SVGA3D_SURFACE_BIND_INDEX_BUFFER)    ? " BIND_INDEX_BUFFER"  : "",
            (f & SVGA3D_SURFACE_BIND_CONSTANT_BUFFER) ? " BIND_CONSTANT_BUFFER"  : "",
            (f & SVGA3D_SURFACE_BIND_SHADER_RESOURCE) ? " BIND_SHADER_RESOURCE"  : "",
            (f & SVGA3D_SURFACE_BIND_RENDER_TARGET)   ? " BIND_RENDER_TARGET"  : "",
            (f & SVGA3D_SURFACE_BIND_DEPTH_STENCIL)   ? " BIND_DEPTH_STENCIL"  : "",
            (f & SVGA3D_SURFACE_BIND_STREAM_OUTPUT)   ? " BIND_STREAM_OUTPUT"  : "",
            (f & SVGA3D_SURFACE_STAGING_UPLOAD)       ? " STAGING_UPLOAD"  : "",
            (f & SVGA3D_SURFACE_STAGING_DOWNLOAD)     ? " STAGING_DOWNLOAD"  : "",
            (f & SVGA3D_SURFACE_HINT_INDIRECT_UPDATE) ? " HINT_INDIRECT_UPDATE"  : "",
            (f & SVGA3D_SURFACE_TRANSFER_FROM_BUFFER) ? " TRANSFER_FROM_BUFFER"  : "",
            (f & SVGA3D_SURFACE_RESERVED1)            ? " RESERVED1"  : "",
            (f & SVGA3D_SURFACE_MULTISAMPLE)          ? " MULTISAMPLE"  : "",
            (f & SVGA3D_SURFACE_BIND_UAVIEW)          ? " BIND_UAVIEW"  : "",
            (f & SVGA3D_SURFACE_TRANSFER_TO_BUFFER)   ? " TRANSFER_TO_BUFFER"  : "",
            (f & SVGA3D_SURFACE_BIND_LOGICOPS)        ? " BIND_LOGICOPS"  : "",
            (f & SVGA3D_SURFACE_BIND_RAW_VIEWS)       ? " BIND_RAW_VIEWS"  : "",
            (f & SVGA3D_SURFACE_BUFFER_STRUCTURED)    ? " BUFFER_STRUCTURED"  : "",
            (f & SVGA3D_SURFACE_DRAWINDIRECT_ARGS)    ? " DRAWINDIRECT_ARGS"  : "",
            (f & SVGA3D_SURFACE_RESOURCE_CLAMP)       ? " RESOURCE_CLAMP"  : "",
            (f & SVGA3D_SURFACE_FLAG_MAX)             ? " FLAG_MAX"  : "",
            f & ~(SVGA3D_SURFACE_FLAG_MAX - 1ULL)
           ));
#endif

    Assert(!VMSVGA3DSURFACE_HAS_HW_SURFACE(pSurface));

    if (fAllocMipLevels || pState->fVMSVGA2dGBO)
    {
        rc = vmsvga3dSurfaceAllocMipLevels(pSurface);
        AssertRCReturn(rc, rc);
    }

    pSurface->id = sid;
    return VINF_SUCCESS;
}


/**
 * Implements the SVGA_3D_CMD_SURFACE_DESTROY command (fifo).
 *
 * @returns VBox status code (currently ignored).
 * @param   pThisCC         The VGA/VMSVGA state for ring-3.
 * @param   sid             The ID of the surface to destroy.
 */
int vmsvga3dSurfaceDestroy(PVGASTATECC pThisCC, uint32_t sid)
{
    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_NO_MEMORY);

    PVMSVGA3DSURFACE pSurface;
    int rc = vmsvga3dSurfaceFromSid(pState, sid, &pSurface);
    AssertRCReturn(rc, rc);

    LogFunc(("sid=%u\n", sid));

    /* Check all contexts if this surface is used as a render target or active texture. */
    for (uint32_t cid = 0; cid < pState->cContexts; cid++)
    {
        PVMSVGA3DCONTEXT pContext = pState->papContexts[cid];
        if (pContext->id == cid)
        {
            for (uint32_t i = 0; i < RT_ELEMENTS(pContext->aSidActiveTextures); ++i)
                if (pContext->aSidActiveTextures[i] == sid)
                    pContext->aSidActiveTextures[i] = SVGA3D_INVALID_ID;
            for (uint32_t i = 0; i < RT_ELEMENTS(pContext->state.aRenderTargets); ++i)
                if (pContext->state.aRenderTargets[i] == sid)
                    pContext->state.aRenderTargets[i] = SVGA3D_INVALID_ID;
        }
    }

    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    if (pSvgaR3State->pFuncs3D)
        pSvgaR3State->pFuncs3D->pfnSurfaceDestroy(pThisCC, true, pSurface);

    if (pSurface->paMipmapLevels)
    {
        vmsvga3dSurfaceFreeMipLevels(pSurface);
        RTMemFree(pSurface->paMipmapLevels);
    }

    memset(pSurface, 0, sizeof(*pSurface));
    pSurface->id = SVGA3D_INVALID_ID;

    return VINF_SUCCESS;
}


/**
 * Implements the SVGA_3D_CMD_SURFACE_STRETCHBLT command (fifo).
 *
 * @returns VBox status code (currently ignored).
 * @param   pThis               The shared VGA/VMSVGA state.
 * @param   pThisCC             The VGA/VMSVGA state for ring-3.
 * @param   pDstSfcImg
 * @param   pDstBox
 * @param   pSrcSfcImg
 * @param   pSrcBox
 * @param   enmMode
 */
int vmsvga3dSurfaceStretchBlt(PVGASTATE pThis, PVGASTATECC pThisCC, SVGA3dSurfaceImageId const *pDstSfcImg, SVGA3dBox const *pDstBox,
                              SVGA3dSurfaceImageId const *pSrcSfcImg, SVGA3dBox const *pSrcBox, SVGA3dStretchBltMode enmMode)
{
    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_NO_MEMORY);

    int rc;

    uint32_t const sidSrc = pSrcSfcImg->sid;
    PVMSVGA3DSURFACE pSrcSurface;
    rc = vmsvga3dSurfaceFromSid(pState, sidSrc, &pSrcSurface);
    AssertRCReturn(rc, rc);

    uint32_t const sidDst = pDstSfcImg->sid;
    PVMSVGA3DSURFACE pDstSurface;
    rc = vmsvga3dSurfaceFromSid(pState, sidDst, &pDstSurface);
    AssertRCReturn(rc, rc);

    AssertReturn(pSrcSfcImg->face < pSrcSurface->cFaces, VERR_INVALID_PARAMETER);
    AssertReturn(pSrcSfcImg->mipmap < pSrcSurface->cLevels, VERR_INVALID_PARAMETER);
    AssertReturn(pDstSfcImg->face < pDstSurface->cFaces, VERR_INVALID_PARAMETER);
    AssertReturn(pDstSfcImg->mipmap < pDstSurface->cLevels, VERR_INVALID_PARAMETER);

    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncs3D, VERR_NOT_IMPLEMENTED);

    PVMSVGA3DCONTEXT pContext;
#ifdef VMSVGA3D_OPENGL
    LogFunc(("src sid=%u (%d,%d)(%d,%d) dest sid=%u (%d,%d)(%d,%d) mode=%x\n",
         sidSrc, pSrcBox->x, pSrcBox->y, pSrcBox->x + pSrcBox->w, pSrcBox->y + pSrcBox->h,
         sidDst, pDstBox->x, pDstBox->y, pDstBox->x + pDstBox->w, pDstBox->y + pDstBox->h, enmMode));
    pContext = &pState->SharedCtx;
    VMSVGA3D_SET_CURRENT_CONTEXT(pState, pContext);
#else
    LogFunc(("src sid=%u cid=%u (%d,%d)(%d,%d) dest sid=%u cid=%u (%d,%d)(%d,%d) mode=%x\n",
         sidSrc, pSrcSurface->idAssociatedContext, pSrcBox->x, pSrcBox->y, pSrcBox->x + pSrcBox->w, pSrcBox->y + pSrcBox->h,
         sidDst, pDstSurface->idAssociatedContext, pDstBox->x, pDstBox->y, pDstBox->x + pDstBox->w, pDstBox->y + pDstBox->h, enmMode));

    uint32_t cid = pDstSurface->idAssociatedContext;
    if (cid == SVGA3D_INVALID_ID)
        cid = pSrcSurface->idAssociatedContext;

    /* At least one of surfaces must be in hardware. */
    AssertReturn(cid != SVGA3D_INVALID_ID, VERR_INVALID_PARAMETER);

    rc = vmsvga3dContextFromCid(pState, cid, &pContext);
    AssertRCReturn(rc, rc);
#endif

    if (!VMSVGA3DSURFACE_HAS_HW_SURFACE(pSrcSurface))
    {
        /* Unknown surface type; turn it into a texture, which can be used for other purposes too. */
        LogFunc(("unknown src sid=%u type=%d format=%d -> create texture\n", sidSrc, pSrcSurface->f.s.surface1Flags, pSrcSurface->format));
        rc = pSvgaR3State->pFuncs3D->pfnCreateTexture(pThisCC, pContext, pContext->id, pSrcSurface);
        AssertRCReturn(rc, rc);
    }

    if (!VMSVGA3DSURFACE_HAS_HW_SURFACE(pDstSurface))
    {
        /* Unknown surface type; turn it into a texture, which can be used for other purposes too. */
        LogFunc(("unknown dest sid=%u type=%d format=%d -> create texture\n", sidDst, pDstSurface->f.s.surface1Flags, pDstSurface->format));
        rc = pSvgaR3State->pFuncs3D->pfnCreateTexture(pThisCC, pContext, pContext->id, pDstSurface);
        AssertRCReturn(rc, rc);
    }

    PVMSVGA3DMIPMAPLEVEL pSrcMipmapLevel;
    rc = vmsvga3dMipmapLevel(pSrcSurface, pSrcSfcImg->face, pSrcSfcImg->mipmap, &pSrcMipmapLevel);
    AssertRCReturn(rc, rc);

    PVMSVGA3DMIPMAPLEVEL pDstMipmapLevel;
    rc = vmsvga3dMipmapLevel(pDstSurface, pDstSfcImg->face, pDstSfcImg->mipmap, &pDstMipmapLevel);
    AssertRCReturn(rc, rc);

    SVGA3dBox clipSrcBox = *pSrcBox;
    SVGA3dBox clipDstBox = *pDstBox;
    vmsvgaR3ClipBox(&pSrcMipmapLevel->mipmapSize, &clipSrcBox);
    vmsvgaR3ClipBox(&pDstMipmapLevel->mipmapSize, &clipDstBox);

    return pSvgaR3State->pFuncs3D->pfnSurfaceStretchBlt(pThis, pState,
                                         pDstSurface, pDstSfcImg->face, pDstSfcImg->mipmap, &clipDstBox,
                                         pSrcSurface, pSrcSfcImg->face, pSrcSfcImg->mipmap, &clipSrcBox,
                                         enmMode, pContext);
}

/**
 * Implements the SVGA_3D_CMD_SURFACE_DMA command (fifo).
 *
 * @returns VBox status code (currently ignored).
 * @param   pThis               The shared VGA/VMSVGA instance data.
 * @param   pThisCC             The VGA/VMSVGA state for ring-3.
 * @param   guest               .
 * @param   host                .
 * @param   transfer            .
 * @param   cCopyBoxes          .
 * @param   paBoxes             .
 */
int vmsvga3dSurfaceDMA(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAGuestImage guest, SVGA3dSurfaceImageId host,
                       SVGA3dTransferType transfer, uint32_t cCopyBoxes, SVGA3dCopyBox *paBoxes)
{
    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_NO_MEMORY);

    PVMSVGA3DSURFACE pSurface;
    int rc = vmsvga3dSurfaceFromSid(pState, host.sid, &pSurface);
    AssertRCReturn(rc, rc);

    LogFunc(("%sguestptr gmr=%x offset=%x pitch=%x host sid=%u face=%d mipmap=%d transfer=%s cCopyBoxes=%d\n",
             (pSurface->f.surfaceFlags & SVGA3D_SURFACE_HINT_TEXTURE) ? "TEXTURE " : "",
             guest.ptr.gmrId, guest.ptr.offset, guest.pitch,
             host.sid, host.face, host.mipmap, (transfer == SVGA3D_WRITE_HOST_VRAM) ? "READ" : "WRITE", cCopyBoxes));

    PVMSVGA3DMIPMAPLEVEL pMipLevel;
    rc = vmsvga3dMipmapLevel(pSurface, host.face, host.mipmap, &pMipLevel);
    AssertRCReturn(rc, rc);

    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncs3D, VERR_NOT_IMPLEMENTED);

    PVMSVGA3DCONTEXT pContext = NULL;
    if (!VMSVGA3DSURFACE_HAS_HW_SURFACE(pSurface))
    {
        /*
         * Not realized in host hardware/library yet, we have to work with
         * the copy of the data we've got in VMSVGA3DMIMAPLEVEL::pSurfaceData.
         */
        if (!pMipLevel->pSurfaceData)
        {
            rc = vmsvga3dSurfaceAllocMipLevels(pSurface);
            AssertRCReturn(rc, rc);
        }
    }
    else if (vmsvga3dIsLegacyBackend(pThisCC))
    {
#ifdef VMSVGA3D_DIRECT3D
        /* Flush the drawing pipeline for this surface as it could be used in a shared context. */
        vmsvga3dSurfaceFlush(pSurface);
#else /* VMSVGA3D_OPENGL */
        pContext = &pState->SharedCtx;
        VMSVGA3D_SET_CURRENT_CONTEXT(pState, pContext);
#endif
    }

    /* SVGA_3D_CMD_SURFACE_DMA:
     * "define the 'source' in each copyBox as the guest image and the
     * 'destination' as the host image, regardless of transfer direction."
     */
    for (uint32_t i = 0; i < cCopyBoxes; ++i)
    {
        Log(("Copy box (%s) %d (%d,%d,%d)(%d,%d,%d) dest (%d,%d)\n",
             VMSVGA3DSURFACE_HAS_HW_SURFACE(pSurface) ? "hw" : "mem",
             i, paBoxes[i].srcx, paBoxes[i].srcy, paBoxes[i].srcz, paBoxes[i].w, paBoxes[i].h, paBoxes[i].d, paBoxes[i].x, paBoxes[i].y));

        /* Apparently we're supposed to clip it (gmr test sample) */

        /* The copybox's "dest" is coords in the host surface. Verify them against the surface's mipmap size. */
        SVGA3dBox hostBox;
        hostBox.x = paBoxes[i].x;
        hostBox.y = paBoxes[i].y;
        hostBox.z = paBoxes[i].z;
        hostBox.w = paBoxes[i].w;
        hostBox.h = paBoxes[i].h;
        hostBox.d = paBoxes[i].d;
        vmsvgaR3ClipBox(&pMipLevel->mipmapSize, &hostBox);

        if (   !hostBox.w
            || !hostBox.h
            || !hostBox.d)
        {
            Log(("Skip empty box\n"));
            continue;
        }
        RT_UNTRUSTED_VALIDATED_FENCE();

        /* Adjust the guest, i.e. "src", point.
         * Do not try to verify them here because vmsvgaR3GmrTransfer takes care of this.
         */
        uint32_t const srcx = paBoxes[i].srcx + (hostBox.x - paBoxes[i].x);
        uint32_t const srcy = paBoxes[i].srcy + (hostBox.y - paBoxes[i].y);
        uint32_t const srcz = paBoxes[i].srcz + (hostBox.z - paBoxes[i].z);

        /* Calculate offsets of the image blocks for the transfer. */
        uint32_t u32HostBlockX;
        uint32_t u32HostBlockY;
        uint32_t u32GuestBlockX;
        uint32_t u32GuestBlockY;
        uint32_t cBlocksX;
        uint32_t cBlocksY;
        if (RT_LIKELY(pSurface->cxBlock == 1 && pSurface->cyBlock == 1))
        {
            u32HostBlockX = hostBox.x;
            u32HostBlockY = hostBox.y;

            u32GuestBlockX = srcx;
            u32GuestBlockY = srcy;

            cBlocksX = hostBox.w;
            cBlocksY = hostBox.h;
        }
        else
        {
            /* Pixels to blocks. */
            u32HostBlockX = hostBox.x / pSurface->cxBlock;
            u32HostBlockY = hostBox.y / pSurface->cyBlock;
            Assert(u32HostBlockX * pSurface->cxBlock == hostBox.x);
            Assert(u32HostBlockY * pSurface->cyBlock == hostBox.y);

            u32GuestBlockX = srcx / pSurface->cxBlock;
            u32GuestBlockY = srcy / pSurface->cyBlock;
            Assert(u32GuestBlockX * pSurface->cxBlock == srcx);
            Assert(u32GuestBlockY * pSurface->cyBlock == srcy);

            cBlocksX = (hostBox.w + pSurface->cxBlock - 1) / pSurface->cxBlock;
            cBlocksY = (hostBox.h + pSurface->cyBlock - 1) / pSurface->cyBlock;
        }

        uint32_t cbGuestPitch = guest.pitch;
        if (cbGuestPitch == 0)
        {
            /* Host must "assume image is tightly packed". Our surfaces are. */
            cbGuestPitch = pMipLevel->cbSurfacePitch;
        }
        else
        {
            /* vmsvgaR3GmrTransfer will verify the value, just check it is sane. */
            AssertReturn(cbGuestPitch <= SVGA3D_MAX_SURFACE_MEM_SIZE, VERR_INVALID_PARAMETER);
            RT_UNTRUSTED_VALIDATED_FENCE();
        }

        /* srcx, srcy and srcz values are used to calculate the guest offset.
         * The offset will be verified by vmsvgaR3GmrTransfer, so just check for overflows here.
         */
        AssertReturn(srcz < UINT32_MAX / pMipLevel->mipmapSize.height / cbGuestPitch, VERR_INVALID_PARAMETER);
        AssertReturn(u32GuestBlockY < UINT32_MAX / cbGuestPitch, VERR_INVALID_PARAMETER);
        AssertReturn(u32GuestBlockX < UINT32_MAX / pSurface->cbBlock, VERR_INVALID_PARAMETER);
        RT_UNTRUSTED_VALIDATED_FENCE();

        if (   !VMSVGA3DSURFACE_HAS_HW_SURFACE(pSurface)
            || VMSVGA3DSURFACE_NEEDS_DATA(pSurface))
        {
            uint64_t uGuestOffset = u32GuestBlockX * pSurface->cbBlock +
                                    u32GuestBlockY * cbGuestPitch +
                                    srcz * pMipLevel->mipmapSize.height * cbGuestPitch;
            AssertReturn(uGuestOffset < UINT32_MAX, VERR_INVALID_PARAMETER);

            /* vmsvga3dSurfaceDefine verifies the surface dimensions and clipBox is within them. */
            uint32_t uHostOffset = u32HostBlockX * pSurface->cbBlock +
                                   u32HostBlockY * pMipLevel->cbSurfacePitch +
                                   hostBox.z * pMipLevel->cbSurfacePlane;
            AssertReturn(uHostOffset < pMipLevel->cbSurface, VERR_INTERNAL_ERROR);

            for (uint32_t z = 0; z < hostBox.d; ++z)
            {
                rc = vmsvgaR3GmrTransfer(pThis,
                                         pThisCC,
                                         transfer,
                                         (uint8_t *)pMipLevel->pSurfaceData,
                                         pMipLevel->cbSurface,
                                         uHostOffset,
                                         (int32_t)pMipLevel->cbSurfacePitch,
                                         guest.ptr,
                                         (uint32_t)uGuestOffset,
                                         cbGuestPitch,
                                         cBlocksX * pSurface->cbBlock,
                                         cBlocksY);
                AssertRC(rc);

                Log4(("first line [z=%d] (updated at offset 0x%x):\n%.*Rhxd\n",
                      z, uHostOffset, pMipLevel->cbSurfacePitch, pMipLevel->pSurfaceData));

                uHostOffset += pMipLevel->cbSurfacePlane;
                uGuestOffset += pMipLevel->mipmapSize.height * cbGuestPitch;
                AssertReturn(uGuestOffset < UINT32_MAX, VERR_INVALID_PARAMETER);
            }
        }

        if (VMSVGA3DSURFACE_HAS_HW_SURFACE(pSurface))
        {
            SVGA3dCopyBox clipBox;
            clipBox.x = hostBox.x;
            clipBox.y = hostBox.y;
            clipBox.z = hostBox.z;
            clipBox.w = hostBox.w;
            clipBox.h = hostBox.h;
            clipBox.d = hostBox.d;
            clipBox.srcx = srcx;
            clipBox.srcy = srcy;
            clipBox.srcz = srcz;
            rc = pSvgaR3State->pFuncs3D->pfnSurfaceDMACopyBox(pThis, pThisCC, pState, pSurface, pMipLevel, host.face, host.mipmap,
                                               guest.ptr, cbGuestPitch, transfer,
                                               &clipBox, pContext, rc, i);
            AssertRC(rc);
        }
    }

    if (!VMSVGA3DSURFACE_HAS_HW_SURFACE(pSurface))
    {
        pMipLevel->fDirty = true;
        pSurface->fDirty = true;
    }

    return rc;
}

static int vmsvga3dQueryWriteResult(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAGuestPtr const *pGuestResult,
                                    SVGA3dQueryState enmState, uint32_t u32Result)
{
    SVGA3dQueryResult queryResult;
    queryResult.totalSize = sizeof(queryResult);    /* Set by guest before query is ended. */
    queryResult.state = enmState;                   /* Set by host or guest. See SVGA3dQueryState. */
    queryResult.result32 = u32Result;

    int rc = vmsvgaR3GmrTransfer(pThis, pThisCC, SVGA3D_READ_HOST_VRAM,
                                 (uint8_t *)&queryResult, sizeof(queryResult), 0, sizeof(queryResult),
                                 *pGuestResult, 0, sizeof(queryResult), sizeof(queryResult), 1);
    AssertRC(rc);
    return rc;
}

/* Used with saved state. */
int vmsvga3dQueryCreate(PVGASTATECC pThisCC, uint32_t cid, SVGA3dQueryType type)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_NO_MEMORY);

    LogFunc(("cid=%u type=%d\n", cid, type));

    PVMSVGA3DCONTEXT pContext;
    int rc = vmsvga3dContextFromCid(pState, cid, &pContext);
    AssertRCReturn(rc, rc);

    if (type == SVGA3D_QUERYTYPE_OCCLUSION)
    {
        VMSVGA3DQUERY *p = &pContext->occlusion;
        if (!VMSVGA3DQUERY_EXISTS(p))
        {
            rc = pSvgaR3State->pFuncsVGPU9->pfnOcclusionQueryCreate(pThisCC, pContext);
            AssertRCReturn(rc, rc);
        }

        return VINF_SUCCESS;
    }

    /* Nothing else for VGPU9. */
    AssertFailedReturn(VERR_NOT_IMPLEMENTED);
}

int vmsvga3dQueryBegin(PVGASTATECC pThisCC, uint32_t cid, SVGA3dQueryType type)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_NO_MEMORY);

    LogFunc(("cid=%u type=%d\n", cid, type));

    PVMSVGA3DCONTEXT pContext;
    int rc = vmsvga3dContextFromCid(pState, cid, &pContext);
    AssertRCReturn(rc, rc);

    if (type == SVGA3D_QUERYTYPE_OCCLUSION)
    {
        VMSVGA3DQUERY *p = &pContext->occlusion;
        if (!VMSVGA3DQUERY_EXISTS(p))
        {
            /* Lazy creation of the query object. */
            rc = pSvgaR3State->pFuncsVGPU9->pfnOcclusionQueryCreate(pThisCC, pContext);
            AssertRCReturn(rc, rc);
        }

        rc = pSvgaR3State->pFuncsVGPU9->pfnOcclusionQueryBegin(pThisCC, pContext);
        AssertRCReturn(rc, rc);

        p->enmQueryState = VMSVGA3DQUERYSTATE_BUILDING;
        p->u32QueryResult = 0;

        return VINF_SUCCESS;
    }

    /* Nothing else for VGPU9. */
    AssertFailedReturn(VERR_NOT_IMPLEMENTED);
}

int vmsvga3dQueryEnd(PVGASTATECC pThisCC, uint32_t cid, SVGA3dQueryType type)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_NO_MEMORY);

    LogFunc(("cid=%u type=%d\n", cid, type));

    PVMSVGA3DCONTEXT pContext;
    int rc = vmsvga3dContextFromCid(pState, cid, &pContext);
    AssertRCReturn(rc, rc);

    if (type == SVGA3D_QUERYTYPE_OCCLUSION)
    {
        VMSVGA3DQUERY *p = &pContext->occlusion;
        Assert(p->enmQueryState == VMSVGA3DQUERYSTATE_BUILDING);
        AssertMsgReturn(VMSVGA3DQUERY_EXISTS(p), ("Query is NULL\n"), VERR_INTERNAL_ERROR);

        rc = pSvgaR3State->pFuncsVGPU9->pfnOcclusionQueryEnd(pThisCC, pContext);
        AssertRCReturn(rc, rc);

        p->enmQueryState = VMSVGA3DQUERYSTATE_ISSUED;
        return VINF_SUCCESS;
    }

    /* Nothing else for VGPU9. */
    AssertFailedReturn(VERR_NOT_IMPLEMENTED);
}

int vmsvga3dQueryWait(PVGASTATECC pThisCC, uint32_t cid, SVGA3dQueryType type, PVGASTATE pThis, SVGAGuestPtr const *pGuestResult)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_NO_MEMORY);

    LogFunc(("cid=%u type=%d guestResult GMR%d:0x%x\n", cid, type, pGuestResult->gmrId, pGuestResult->offset));

    PVMSVGA3DCONTEXT pContext;
    int rc = vmsvga3dContextFromCid(pState, cid, &pContext);
    AssertRCReturn(rc, rc);

    if (type == SVGA3D_QUERYTYPE_OCCLUSION)
    {
        VMSVGA3DQUERY *p = &pContext->occlusion;
        if (VMSVGA3DQUERY_EXISTS(p))
        {
            if (p->enmQueryState == VMSVGA3DQUERYSTATE_ISSUED)
            {
                /* Only if not already in SIGNALED state,
                 * i.e. not a second read from the guest or after restoring saved state.
                 */
                uint32_t u32Pixels = 0;
                rc = pSvgaR3State->pFuncsVGPU9->pfnOcclusionQueryGetData(pThisCC, pContext, &u32Pixels);
                if (RT_SUCCESS(rc))
                {
                    p->enmQueryState = VMSVGA3DQUERYSTATE_SIGNALED;
                    p->u32QueryResult += u32Pixels; /* += because it might contain partial result from saved state. */
                }
            }

            if (RT_SUCCESS(rc))
            {
                /* pGuestResult can be NULL when saving the state. */
                if (pGuestResult)
                {
                    /* Return data to the guest. */
                    vmsvga3dQueryWriteResult(pThis, pThisCC, pGuestResult, SVGA3D_QUERYSTATE_SUCCEEDED, p->u32QueryResult);
                }
                return VINF_SUCCESS;
            }
        }
        else
        {
            AssertMsgFailed(("GetData Query is NULL\n"));
        }

        rc = VERR_INTERNAL_ERROR;
    }
    else
    {
        rc = VERR_NOT_IMPLEMENTED;
    }

    if (pGuestResult)
        vmsvga3dQueryWriteResult(pThis, pThisCC, pGuestResult, SVGA3D_QUERYSTATE_FAILED, 0);
    AssertFailedReturn(rc);
}

int vmsvga3dSurfaceBlitToScreen(PVGASTATE pThis, PVGASTATECC pThisCC, uint32_t idDstScreen, SVGASignedRect destRect,
                                SVGA3dSurfaceImageId srcImage, SVGASignedRect srcRect, uint32_t cRects, SVGASignedRect *pRect)
{
    /* Requires SVGA_FIFO_CAP_SCREEN_OBJECT support */
    LogFunc(("dest=%d (%d,%d)(%d,%d) sid=%u (face=%d, mipmap=%d) (%d,%d)(%d,%d) cRects=%d\n",
             idDstScreen, destRect.left, destRect.top, destRect.right, destRect.bottom, srcImage.sid, srcImage.face, srcImage.mipmap,
             srcRect.left, srcRect.top, srcRect.right, srcRect.bottom, cRects));
    for (uint32_t i = 0; i < cRects; i++)
    {
        LogFunc(("clipping rect[%d] (%d,%d)(%d,%d)\n", i, pRect[i].left, pRect[i].top, pRect[i].right, pRect[i].bottom));
    }

    VMSVGASCREENOBJECT *pScreen = vmsvgaR3GetScreenObject(pThisCC, idDstScreen);
    AssertReturn(pScreen, VERR_INTERNAL_ERROR);

    /* vmwgfx driver does not always initialize srcImage.mipmap and srcImage.face. They are assumed to be zero. */
    SVGA3dSurfaceImageId src;
    src.sid = srcImage.sid;
    src.mipmap = 0;
    src.face = 0;

    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    if (pScreen->pHwScreen)
    {
        /* Use the backend accelerated method, if available. */
        if (pSvgaR3State->pFuncs3D)
        {
            int rc = pSvgaR3State->pFuncs3D->pfnSurfaceBlitToScreen(pThisCC, pScreen, destRect, src, srcRect, cRects, pRect);
            if (rc == VINF_SUCCESS)
            {
                return VINF_SUCCESS;
            }
        }
    }

    if (pSvgaR3State->pFuncsMap)
        return vmsvga3dScreenUpdate(pThisCC, idDstScreen, destRect, src, srcRect, cRects, pRect);

    /** @todo scaling */
    AssertReturn(destRect.right - destRect.left == srcRect.right - srcRect.left && destRect.bottom - destRect.top == srcRect.bottom - srcRect.top, VERR_INVALID_PARAMETER);

    SVGA3dCopyBox    box;
    SVGAGuestImage dest;

    box.srcz = 0;
    box.z    = 0;
    box.d    = 1;

    dest.ptr.gmrId  = SVGA_GMR_FRAMEBUFFER;
    dest.ptr.offset = pScreen->offVRAM;
    dest.pitch      = pScreen->cbPitch;

    if (cRects == 0)
    {
        /* easy case; no clipping */

        /* SVGA_3D_CMD_SURFACE_DMA:
         * 'define the "source" in each copyBox as the guest image and the
         * "destination" as the host image, regardless of transfer direction.'
         *
         * Since the BlitToScreen operation transfers from a host surface to the guest VRAM,
         * it must set the copyBox "source" to the guest destination coords and
         * the copyBox "destination" to the host surface source coords.
         */
        /* Host image. */
        box.x       = srcRect.left;
        box.y       = srcRect.top;
        box.w       = srcRect.right - srcRect.left;
        box.h       = srcRect.bottom - srcRect.top;
        /* Guest image. */
        box.srcx    = destRect.left;
        box.srcy    = destRect.top;

        int rc = vmsvga3dSurfaceDMA(pThis, pThisCC, dest, src, SVGA3D_READ_HOST_VRAM, 1, &box);
        AssertRCReturn(rc, rc);

        /* Update the guest image, which is at box.src. */
        vmsvgaR3UpdateScreen(pThisCC, pScreen, box.srcx, box.srcy, box.w, box.h);
    }
    else
    {
        /** @todo merge into one SurfaceDMA call */
        for (uint32_t i = 0; i < cRects; i++)
        {
            /* "The clip rectangle coordinates are measured
             * relative to the top-left corner of destRect."
             * Therefore they are relative to the top-left corner of srcRect as well.
             */

            /* Host image. See 'SVGA_3D_CMD_SURFACE_DMA:' comment in the 'if' branch. */
            box.x    = srcRect.left + pRect[i].left;
            box.y    = srcRect.top  + pRect[i].top;
            box.w    = pRect[i].right - pRect[i].left;
            box.h    = pRect[i].bottom - pRect[i].top;
            /* Guest image. The target screen memory is currently in the guest VRAM. */
            box.srcx = destRect.left + pRect[i].left;
            box.srcy = destRect.top  + pRect[i].top;

            int rc = vmsvga3dSurfaceDMA(pThis, pThisCC, dest, src, SVGA3D_READ_HOST_VRAM, 1, &box);
            AssertRCReturn(rc, rc);

            /* Update the guest image, which is at box.src. */
            vmsvgaR3UpdateScreen(pThisCC, pScreen, box.srcx, box.srcy, box.w, box.h);
        }
    }

    return VINF_SUCCESS;
}

int vmsvga3dScreenUpdate(PVGASTATECC pThisCC, uint32_t idDstScreen, SVGASignedRect const &dstRect,
                         SVGA3dSurfaceImageId const &srcImage, SVGASignedRect const &srcRect,
                         uint32_t cDstClipRects, SVGASignedRect *paDstClipRect)
{
    //DEBUG_BREAKPOINT_TEST();
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

#ifdef LOG_ENABLED
    LogFunc(("[%u] %d,%d %d,%d (%dx%d) -> %d,%d %d,%d (%dx%d), %u clip rects\n",
             idDstScreen, srcRect.left, srcRect.top, srcRect.right, srcRect.bottom,
             srcRect.right - srcRect.left, srcRect.bottom - srcRect.top,
             dstRect.left, dstRect.top, dstRect.right, dstRect.bottom,
             dstRect.right - dstRect.left, dstRect.bottom - dstRect.top, cDstClipRects));
    for (uint32_t i = 0; i < cDstClipRects; i++)
    {
        LogFunc(("  [%u] %d,%d %d,%d (%dx%d)\n",
                 i, paDstClipRect[i].left, paDstClipRect[i].top, paDstClipRect[i].right, paDstClipRect[i].bottom,
                 paDstClipRect[i].right - paDstClipRect[i].left, paDstClipRect[i].bottom - paDstClipRect[i].top));
    }
#endif

    PVMSVGA3DSURFACE pSurface;
    int rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, srcImage.sid, &pSurface);
    AssertRCReturn(rc, rc);

    /* Update the screen from a surface. */
    ASSERT_GUEST_RETURN(idDstScreen < RT_ELEMENTS(pSvgaR3State->aScreens), VERR_INVALID_PARAMETER);
    RT_UNTRUSTED_VALIDATED_FENCE();

    VMSVGASCREENOBJECT *pScreen = &pSvgaR3State->aScreens[idDstScreen];

    uint32_t const cbScreenPixel = (pScreen->cBpp + 7) / 8;
    ASSERT_GUEST_RETURN(cbScreenPixel == pSurface->cbBlock,
                        VERR_INVALID_PARAMETER); /* Format conversion is not supported. */

    if (   srcRect.right <= srcRect.left
        || srcRect.bottom <= srcRect.top)
        return VINF_SUCCESS; /* Empty src rect. */

    if (   dstRect.right <= dstRect.left
        || dstRect.bottom <= dstRect.top)
        return VINF_SUCCESS; /* Empty dst rect. */
    RT_UNTRUSTED_VALIDATED_FENCE();

    ASSERT_GUEST_RETURN(   srcRect.right - srcRect.left == dstRect.right - dstRect.left
                        && srcRect.bottom - srcRect.top == dstRect.bottom - dstRect.top,
                        VERR_INVALID_PARAMETER); /* Stretch is not supported. */

    /* Destination box should be within the screen rectangle. */
    SVGA3dBox dstBox;
    dstBox.x = dstRect.left;
    dstBox.y = dstRect.top;
    dstBox.z = 0;
    dstBox.w = dstRect.right - dstRect.left;
    dstBox.h = dstRect.bottom - dstRect.top;
    dstBox.d = 1;

    SVGA3dSize dstClippingSize;
    dstClippingSize.width = pScreen->cWidth;
    dstClippingSize.height = pScreen->cHeight;
    dstClippingSize.depth = 1;

    vmsvgaR3ClipBox(&dstClippingSize, &dstBox);
    ASSERT_GUEST_RETURN(dstBox.w > 0 && dstBox.h > 0, VERR_INVALID_PARAMETER);
    RT_UNTRUSTED_VALIDATED_FENCE();

    /* All dst clip rects will be clipped by the dst box because
     * "The clip rectangle coordinates are measured relative to the top-left corner of destRect."
     */
    dstClippingSize.width = dstBox.w;
    dstClippingSize.height = dstBox.h;
    dstClippingSize.depth = 1;

    SVGA3dBox srcBox; /* SurfaceMap will clip the box as necessary (srcMap.box). */
    srcBox.x = srcRect.left;
    srcBox.y = srcRect.top;
    srcBox.z = 0;
    srcBox.w = srcRect.right - srcRect.left;
    srcBox.h = srcRect.bottom - srcRect.top;
    srcBox.d = 1;

    VMSVGA3D_MAPPED_SURFACE srcMap;
    rc = vmsvga3dSurfaceMap(pThisCC, &srcImage, &srcBox, VMSVGA3D_SURFACE_MAP_READ, VMSVGA3D_MAP_F_NONE, &srcMap);
    if (RT_SUCCESS(rc))
    {
        /* Clipping rectangle. */
        SVGASignedRect srcBoundRect;
        srcBoundRect.left   = srcMap.box.x;
        srcBoundRect.top    = srcMap.box.y;
        srcBoundRect.right  = srcMap.box.x + srcMap.box.w;
        srcBoundRect.bottom = srcMap.box.y + srcMap.box.h;

        /* Clipping rectangle relative to the original srcRect. */
        srcBoundRect.left   -= srcRect.left;
        srcBoundRect.top    -= srcRect.top;
        srcBoundRect.right  -= srcRect.left;
        srcBoundRect.bottom -= srcRect.top;

        uint8_t const *pu8Src = (uint8_t *)srcMap.pvData;

        uint32_t const cbDst = pScreen->cHeight * pScreen->cbPitch;
        uint8_t *pu8Dst;
        if (pScreen->pvScreenBitmap)
            pu8Dst = (uint8_t *)pScreen->pvScreenBitmap;
        else
            pu8Dst = (uint8_t *)pThisCC->pbVRam + pScreen->offVRAM;

        SVGASignedRect dstClipRect;
        if (cDstClipRects == 0)
        {
            /* Entire source rect "relative to the top-left corner of destRect." */
            dstClipRect.left   = 0;
            dstClipRect.top    = 0;
            dstClipRect.right  = dstBox.w;
            dstClipRect.bottom = dstBox.h;

            cDstClipRects = 1;
            paDstClipRect = &dstClipRect;
        }

        for (uint32_t i = 0; i < cDstClipRects; i++)
        {
            /* Clip rects are relative to corners of src and dst rectangles. */
            SVGASignedRect clipRect = paDstClipRect[i];

            /* Clip the rectangle by mapped source box. */
            vmsvgaR3ClipRect(&srcBoundRect, &clipRect);

            SVGA3dBox clipBox;
            clipBox.x = clipRect.left;
            clipBox.y = clipRect.top;
            clipBox.z = 0;
            clipBox.w = clipRect.right - clipRect.left;
            clipBox.h = clipRect.bottom - clipRect.top;
            clipBox.d = 1;

            vmsvgaR3ClipBox(&dstClippingSize, &clipBox);
            ASSERT_GUEST_CONTINUE(clipBox.w > 0 && clipBox.h > 0);

            /* 'pu8Src' points to the mapped 'srcRect'. Take the clipping box into account. */
            uint8_t const *pu8SrcBox = pu8Src
                + ((clipBox.x + pSurface->cxBlock - 1) / pSurface->cxBlock) * pSurface->cxBlock * pSurface->cbBlock
                + ((clipBox.y + pSurface->cyBlock - 1) / pSurface->cyBlock) * pSurface->cyBlock * srcMap.cbRowPitch;

            /* Calculate the offset of destination box in the screen buffer. */
            uint32_t const offDstBox = (dstBox.x + clipBox.x) * cbScreenPixel + (dstBox.y + clipBox.y) * pScreen->cbPitch;

            ASSERT_GUEST_BREAK(   offDstBox <= cbDst
                               && pScreen->cbPitch * (clipBox.h - 1) + cbScreenPixel * clipBox.w <= cbDst - offDstBox);
            RT_UNTRUSTED_VALIDATED_FENCE();

            uint8_t *pu8DstBox = pu8Dst + offDstBox;

            if (   pSurface->format == SVGA3D_R8G8B8A8_UNORM
                || pSurface->format == SVGA3D_R8G8B8A8_UNORM_SRGB)
            {
                for (uint32_t iRow = 0; iRow < clipBox.h; ++iRow)
                {
                    for (uint32_t x = 0; x < clipBox.w * 4; x += 4) /* 'x' is a byte index. */
                    {
                        pu8DstBox[x    ] = pu8SrcBox[x + 2];
                        pu8DstBox[x + 1] = pu8SrcBox[x + 1];
                        pu8DstBox[x + 2] = pu8SrcBox[x    ];
                        pu8DstBox[x + 3] = pu8SrcBox[x + 3];
                    }

                    pu8SrcBox += srcMap.cbRowPitch;
                    pu8DstBox += pScreen->cbPitch;
                }
            }
            else
            {
                for (uint32_t iRow = 0; iRow < clipBox.h; ++iRow)
                {
                    memcpy(pu8DstBox, pu8SrcBox, cbScreenPixel * clipBox.w);

                    pu8SrcBox += srcMap.cbRowPitch;
                    pu8DstBox += pScreen->cbPitch;
                }
            }
        }

        vmsvga3dSurfaceUnmap(pThisCC, &srcImage, &srcMap, /* fWritten =  */ false);

        vmsvgaR3UpdateScreen(pThisCC, pScreen, dstBox.x, dstBox.y, dstBox.w, dstBox.h);
    }

    return rc;
}

int vmsvga3dCommandPresent(PVGASTATE pThis, PVGASTATECC pThisCC, uint32_t sid, uint32_t cRects, SVGA3dCopyRect *pRect)
{
    /* Deprecated according to svga3d_reg.h. */
    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_NO_MEMORY);

    PVMSVGA3DSURFACE pSurface;
    int rc = vmsvga3dSurfaceFromSid(pState, sid, &pSurface);
    AssertRCReturn(rc, rc);

    /** @todo Detect screen from coords? Or split rect to screens? */
    VMSVGASCREENOBJECT *pScreen = vmsvgaR3GetScreenObject(pThisCC, 0);
    AssertReturn(pScreen, VERR_INTERNAL_ERROR);

    /* If there are no recangles specified, just grab a screenful. */
    SVGA3dCopyRect DummyRect;
    if (cRects != 0)
    { /* likely */ }
    else
    {
        /** @todo Find the usecase for this or check what the original device does.
         *        The original code was doing some scaling based on the surface
         *        size... */
        AssertMsgFailed(("No rects to present. Who is doing that and what do they actually expect?\n"));
        DummyRect.x = DummyRect.srcx = 0;
        DummyRect.y = DummyRect.srcy = 0;
        DummyRect.w = pScreen->cWidth;
        DummyRect.h = pScreen->cHeight;
        cRects = 1;
        pRect  = &DummyRect;
    }

    uint32_t i;
    for (i = 0; i < cRects; ++i)
    {
        uint32_t idDstScreen = 0; /** @todo Use virtual coords: SVGA_ID_INVALID. */
        SVGASignedRect destRect;
        destRect.left   = pRect[i].x;
        destRect.top    = pRect[i].y;
        destRect.right  = pRect[i].x + pRect[i].w;
        destRect.bottom = pRect[i].y + pRect[i].h;

        SVGA3dSurfaceImageId src;
        src.sid = sid;
        src.face = 0;
        src.mipmap = 0;

        SVGASignedRect srcRect;
        srcRect.left   = pRect[i].srcx;
        srcRect.top    = pRect[i].srcy;
        srcRect.right  = pRect[i].srcx + pRect[i].w;
        srcRect.bottom = pRect[i].srcy + pRect[i].h;

        /* Entire rect. */
        rc = vmsvga3dSurfaceBlitToScreen(pThis, pThisCC, idDstScreen, destRect, src, srcRect, 0, NULL);
        AssertRCReturn(rc, rc);
    }

    return VINF_SUCCESS;
}

int vmsvga3dDefineScreen(PVGASTATE pThis, PVGASTATECC pThisCC, VMSVGASCREENOBJECT *pScreen)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncs3D, VERR_NOT_IMPLEMENTED);

    if (pScreen->pHwScreen)
    {
        pSvgaR3State->pFuncs3D->pfnDestroyScreen(pThisCC, pScreen);
    }

    int rc = pSvgaR3State->pFuncs3D->pfnDefineScreen(pThis, pThisCC, pScreen);
    if (RT_SUCCESS(rc))
    {
        LogRelMax(1, ("VMSVGA: using accelerated graphics output\n"));
    }
    return rc;
}

int vmsvga3dDestroyScreen(PVGASTATECC pThisCC, VMSVGASCREENOBJECT *pScreen)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncs3D, VERR_NOT_IMPLEMENTED);

    return pSvgaR3State->pFuncs3D->pfnDestroyScreen(pThisCC, pScreen);
}

int vmsvga3dSurfaceInvalidate(PVGASTATECC pThisCC, uint32_t sid, uint32_t face, uint32_t mipmap)
{
    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_INVALID_STATE);

    PVMSVGA3DSURFACE pSurface;
    int rc = vmsvga3dSurfaceFromSid(pState, sid, &pSurface);
    AssertRCReturn(rc, rc);

    if (face == SVGA_ID_INVALID && mipmap == SVGA_ID_INVALID)
    {
        /* This is a notification that "All images can be lost", i.e. the backend surface is not needed anymore. */
        PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
        if (pSvgaR3State->pFuncs3D)
            pSvgaR3State->pFuncs3D->pfnSurfaceDestroy(pThisCC, false, pSurface);

        for (uint32_t i = 0; i < pSurface->cLevels * pSurface->surfaceDesc.numArrayElements; ++i)
        {
            PVMSVGA3DMIPMAPLEVEL pMipmapLevel = &pSurface->paMipmapLevels[i];
            pMipmapLevel->fDirty = true;
        }
    }
    else
    {
        PVMSVGA3DMIPMAPLEVEL pMipmapLevel;
        rc = vmsvga3dMipmapLevel(pSurface, face, mipmap, &pMipmapLevel);
        AssertRCReturn(rc, rc);

        /* Invalidate views, etc. */
        PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
        if (pSvgaR3State->pFuncs3D)
            pSvgaR3State->pFuncs3D->pfnSurfaceInvalidateImage(pThisCC, pSurface, face, mipmap);

        pMipmapLevel->fDirty = true;
    }
    pSurface->fDirty = true;

    return rc;
}


/*
 *
 * 3D
 *
 */

int vmsvga3dQueryCaps(PVGASTATECC pThisCC, SVGA3dDevCapIndex idx3dCaps, uint32_t *pu32Val)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncs3D, VERR_NOT_IMPLEMENTED);
    return pSvgaR3State->pFuncs3D->pfnQueryCaps(pThisCC, idx3dCaps, pu32Val);
}

int vmsvga3dChangeMode(PVGASTATECC pThisCC)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncs3D, VERR_NOT_IMPLEMENTED);
    return pSvgaR3State->pFuncs3D->pfnChangeMode(pThisCC);
}

int vmsvga3dSurfaceCopySysMem(PVMSVGA3DSTATE pState, SVGA3dSurfaceImageId dest, SVGA3dSurfaceImageId src,
                               uint32_t cCopyBoxes, SVGA3dCopyBox *pBox)
{
    RT_NOREF(cCopyBoxes);
    AssertReturn(pBox, VERR_INVALID_PARAMETER);

    LogFunc(("src sid %d -> dst sid %d\n", src.sid, dest.sid));

    AssertReturn(pState, VERR_INVALID_STATE);

    PVMSVGA3DSURFACE pSrcSurface;
    int rc = vmsvga3dSurfaceFromSid(pState, src.sid, &pSrcSurface);
    AssertRCReturn(rc, rc);

    PVMSVGA3DSURFACE pDstSurface;
    rc = vmsvga3dSurfaceFromSid(pState, dest.sid, &pDstSurface);
    AssertRCReturn(rc, rc);

    PVMSVGA3DMIPMAPLEVEL pSrcMipLevel;
    rc = vmsvga3dMipmapLevel(pSrcSurface, src.face, src.mipmap, &pSrcMipLevel);
    ASSERT_GUEST_RETURN(RT_SUCCESS(rc), rc);

    PVMSVGA3DMIPMAPLEVEL pDstMipLevel;
    rc = vmsvga3dMipmapLevel(pDstSurface, dest.face, dest.mipmap, &pDstMipLevel);
    ASSERT_GUEST_RETURN(RT_SUCCESS(rc), rc);

    SVGA3dCopyBox clipBox = *pBox;
    vmsvgaR3ClipCopyBox(&pSrcMipLevel->mipmapSize, &pDstMipLevel->mipmapSize, &clipBox);

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

int vmsvga3dSurfaceCopy(PVGASTATECC pThisCC, SVGA3dSurfaceImageId dest, SVGA3dSurfaceImageId src, uint32_t cCopyBoxes, SVGA3dCopyBox *pBox)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    PVMSVGA3DSTATE const p3dState     = pThisCC->svga.p3dState;
    AssertReturn(pSvgaR3State && p3dState, VERR_INVALID_STATE);

    if (!p3dState->fVMSVGA2dGBO)
    {
        AssertReturn(pSvgaR3State->pFuncs3D, VERR_NOT_IMPLEMENTED);
        return pSvgaR3State->pFuncs3D->pfnSurfaceCopy(pThisCC, dest, src, cCopyBoxes, pBox);
    }
    else
    {
        return vmsvga3dSurfaceCopySysMem(p3dState, dest, src, cCopyBoxes, pBox);
    }
}

void vmsvga3dUpdateHostScreenViewport(PVGASTATECC pThisCC, uint32_t idScreen, VMSVGAVIEWPORT const *pOldViewport)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturnVoid(pSvgaR3State->pFuncs3D);
    pSvgaR3State->pFuncs3D->pfnUpdateHostScreenViewport(pThisCC, idScreen, pOldViewport);
}

/**
 * Updates the heap buffers for all surfaces or one specific one.
 *
 * @param   pThisCC     The VGA/VMSVGA state for ring-3.
 * @param   sid         The surface ID, UINT32_MAX if all.
 * @thread  VMSVGAFIFO
 */
void vmsvga3dUpdateHeapBuffersForSurfaces(PVGASTATECC pThisCC, uint32_t sid)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturnVoid(pSvgaR3State->pFuncs3D);

    PVMSVGA3DSTATE pState = pThisCC->svga.p3dState;
    AssertReturnVoid(pState);

    if (sid == UINT32_MAX)
    {
        uint32_t cSurfaces = pState->cSurfaces;
        for (sid = 0; sid < cSurfaces; sid++)
        {
            PVMSVGA3DSURFACE pSurface = pState->papSurfaces[sid];
            if (pSurface && pSurface->id == sid)
                pSvgaR3State->pFuncs3D->pfnSurfaceUpdateHeapBuffers(pThisCC, pSurface);
        }
    }
    else if (sid < pState->cSurfaces)
    {
        PVMSVGA3DSURFACE pSurface = pState->papSurfaces[sid];
        if (pSurface && pSurface->id == sid)
            pSvgaR3State->pFuncs3D->pfnSurfaceUpdateHeapBuffers(pThisCC, pSurface);
    }
}


/*
 *
 * VGPU9
 *
 */

int vmsvga3dContextDefine(PVGASTATECC pThisCC, uint32_t cid)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);
    return pSvgaR3State->pFuncsVGPU9->pfnContextDefine(pThisCC, cid);
}

int vmsvga3dContextDestroy(PVGASTATECC pThisCC, uint32_t cid)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);
    return pSvgaR3State->pFuncsVGPU9->pfnContextDestroy(pThisCC, cid);
}

int vmsvga3dSetTransform(PVGASTATECC pThisCC, uint32_t cid, SVGA3dTransformType type, float matrix[16])
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);
    return pSvgaR3State->pFuncsVGPU9->pfnSetTransform(pThisCC, cid, type, matrix);
}

int vmsvga3dSetZRange(PVGASTATECC pThisCC, uint32_t cid, SVGA3dZRange zRange)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);
    return pSvgaR3State->pFuncsVGPU9->pfnSetZRange(pThisCC, cid, zRange);
}

int vmsvga3dSetRenderState(PVGASTATECC pThisCC, uint32_t cid, uint32_t cRenderStates, SVGA3dRenderState *pRenderState)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);
    return pSvgaR3State->pFuncsVGPU9->pfnSetRenderState(pThisCC, cid, cRenderStates, pRenderState);
}

int vmsvga3dSetRenderTarget(PVGASTATECC pThisCC, uint32_t cid, SVGA3dRenderTargetType type, SVGA3dSurfaceImageId target)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);
    return pSvgaR3State->pFuncsVGPU9->pfnSetRenderTarget(pThisCC, cid, type, target);
}

int vmsvga3dSetTextureState(PVGASTATECC pThisCC, uint32_t cid, uint32_t cTextureStates, SVGA3dTextureState *pTextureState)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);
    return pSvgaR3State->pFuncsVGPU9->pfnSetTextureState(pThisCC, cid, cTextureStates, pTextureState);
}

int vmsvga3dSetMaterial(PVGASTATECC pThisCC, uint32_t cid, SVGA3dFace face, SVGA3dMaterial *pMaterial)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);
    return pSvgaR3State->pFuncsVGPU9->pfnSetMaterial(pThisCC, cid, face, pMaterial);
}

int vmsvga3dSetLightData(PVGASTATECC pThisCC, uint32_t cid, uint32_t index, SVGA3dLightData *pData)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);
    return pSvgaR3State->pFuncsVGPU9->pfnSetLightData(pThisCC, cid, index, pData);
}

int vmsvga3dSetLightEnabled(PVGASTATECC pThisCC, uint32_t cid, uint32_t index, uint32_t enabled)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);
    return pSvgaR3State->pFuncsVGPU9->pfnSetLightEnabled(pThisCC, cid, index, enabled);
}

int vmsvga3dSetViewPort(PVGASTATECC pThisCC, uint32_t cid, SVGA3dRect *pRect)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);
    return pSvgaR3State->pFuncsVGPU9->pfnSetViewPort(pThisCC, cid, pRect);
}

int vmsvga3dSetClipPlane(PVGASTATECC pThisCC, uint32_t cid,  uint32_t index, float plane[4])
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);
    return pSvgaR3State->pFuncsVGPU9->pfnSetClipPlane(pThisCC, cid, index, plane);
}

int vmsvga3dCommandClear(PVGASTATECC pThisCC, uint32_t cid, SVGA3dClearFlag clearFlag, uint32_t color, float depth, uint32_t stencil, uint32_t cRects, SVGA3dRect *pRect)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);
    return pSvgaR3State->pFuncsVGPU9->pfnCommandClear(pThisCC, cid, clearFlag, color, depth, stencil, cRects, pRect);
}

int vmsvga3dDrawPrimitives(PVGASTATECC pThisCC, uint32_t cid, uint32_t numVertexDecls, SVGA3dVertexDecl *pVertexDecl, uint32_t numRanges, SVGA3dPrimitiveRange *pNumRange, uint32_t cVertexDivisor, SVGA3dVertexDivisor *pVertexDivisor)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);
    return pSvgaR3State->pFuncsVGPU9->pfnDrawPrimitives(pThisCC, cid, numVertexDecls, pVertexDecl, numRanges, pNumRange, cVertexDivisor, pVertexDivisor);
}

int vmsvga3dSetScissorRect(PVGASTATECC pThisCC, uint32_t cid, SVGA3dRect *pRect)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);
    return pSvgaR3State->pFuncsVGPU9->pfnSetScissorRect(pThisCC, cid, pRect);
}

int vmsvga3dGenerateMipmaps(PVGASTATECC pThisCC, uint32_t sid, SVGA3dTextureFilter filter)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);
    return pSvgaR3State->pFuncsVGPU9->pfnGenerateMipmaps(pThisCC, sid, filter);
}

int vmsvga3dShaderDefine(PVGASTATECC pThisCC, uint32_t cid, uint32_t shid, SVGA3dShaderType type, uint32_t cbData, uint32_t *pShaderData)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);
    return pSvgaR3State->pFuncsVGPU9->pfnShaderDefine(pThisCC, cid, shid, type, cbData, pShaderData);
}

int vmsvga3dShaderDestroy(PVGASTATECC pThisCC, uint32_t cid, uint32_t shid, SVGA3dShaderType type)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);
    return pSvgaR3State->pFuncsVGPU9->pfnShaderDestroy(pThisCC, cid, shid, type);
}

int vmsvga3dShaderSet(PVGASTATECC pThisCC, struct VMSVGA3DCONTEXT *pContext, uint32_t cid, SVGA3dShaderType type, uint32_t shid)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);
    return pSvgaR3State->pFuncsVGPU9->pfnShaderSet(pThisCC, pContext, cid, type, shid);
}

int vmsvga3dShaderSetConst(PVGASTATECC pThisCC, uint32_t cid, uint32_t reg, SVGA3dShaderType type, SVGA3dShaderConstType ctype, uint32_t cRegisters, uint32_t *pValues)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    AssertReturn(pSvgaR3State->pFuncsVGPU9, VERR_NOT_IMPLEMENTED);
    return pSvgaR3State->pFuncsVGPU9->pfnShaderSetConst(pThisCC, cid, reg, type, ctype, cRegisters, pValues);
}


/*
 *
 * Map
 *
 */

void vmsvga3dSurfaceMapInit(VMSVGA3D_MAPPED_SURFACE *pMap, VMSVGA3D_SURFACE_MAP enmMapType, uint32_t fMapFlags, SVGA3dBox const *pBox,
                            PVMSVGA3DSURFACE pSurface, void *pvData, uint32_t cbRowPitch, uint32_t cbDepthPitch)
{
    uint32_t const cxBlocks = (pBox->w + pSurface->cxBlock - 1) / pSurface->cxBlock;
    uint32_t const cyBlocks = (pBox->h + pSurface->cyBlock - 1) / pSurface->cyBlock;

    pMap->enmMapType   = enmMapType;
    pMap->format       = pSurface->format;
    pMap->box          = *pBox;
    pMap->cbBlock      = pSurface->cbBlock;
    pMap->cxBlocks     = cxBlocks;
    pMap->cyBlocks     = cyBlocks;
    pMap->cbRow        = cxBlocks * pSurface->cbPitchBlock;
    pMap->cbRowPitch   = cbRowPitch;
    pMap->cRows        = (cyBlocks * pSurface->cbBlock) / pSurface->cbPitchBlock;
    pMap->cbDepthPitch = cbDepthPitch;
    pMap->fMapFlags    = fMapFlags;
    pMap->pvData       = (uint8_t *)pvData
                       + (pBox->x / pSurface->cxBlock) * pSurface->cbPitchBlock
                       + (pBox->y / pSurface->cyBlock) * cbRowPitch
                       + pBox->z * cbDepthPitch;
    pMap->pvBackendResource = NULL;
}


int vmsvga3dSurfaceMap(PVGASTATECC pThisCC, SVGA3dSurfaceImageId const *pImage, SVGA3dBox const *pBox,
                       VMSVGA3D_SURFACE_MAP enmMapType, uint32_t fMapFlags, VMSVGA3D_MAPPED_SURFACE *pMap)
{
    PVMSVGA3DSURFACE pSurface;
    int rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, pImage->sid, &pSurface);
    AssertRCReturn(rc, rc);

    if (VMSVGA3DSURFACE_HAS_HW_SURFACE(pSurface))
    {
        PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
        AssertReturn(pSvgaR3State->pFuncsMap, VERR_NOT_IMPLEMENTED);
        return pSvgaR3State->pFuncsMap->pfnSurfaceMap(pThisCC, pImage, pBox, enmMapType, fMapFlags, pMap);
    }

    PVMSVGA3DMIPMAPLEVEL pMipLevel;
    rc = vmsvga3dMipmapLevel(pSurface, pImage->face, pImage->mipmap, &pMipLevel);
    ASSERT_GUEST_RETURN(RT_SUCCESS(rc), rc);

    if (!pMipLevel->pSurfaceData)
    {
        rc = vmsvga3dSurfaceAllocMipLevels(pSurface);
        AssertRCReturn(rc, rc);
    }

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

    /// @todo Zero the box?
    //if (enmMapType == VMSVGA3D_SURFACE_MAP_WRITE_DISCARD)
    //    RT_BZERO(.);

    vmsvga3dSurfaceMapInit(pMap, enmMapType, fMapFlags, &clipBox, pSurface,
                           pMipLevel->pSurfaceData, pMipLevel->cbSurfacePitch, pMipLevel->cbSurfacePlane);

    LogFunc(("SysMem: sid = %u, pvData %p\n", pImage->sid, pMap->pvData));
    return VINF_SUCCESS;
}

int vmsvga3dSurfaceUnmap(PVGASTATECC pThisCC, SVGA3dSurfaceImageId const *pImage, VMSVGA3D_MAPPED_SURFACE *pMap, bool fWritten)
{
    PVMSVGA3DSURFACE pSurface;
    int rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, pImage->sid, &pSurface);
    AssertRCReturn(rc, rc);

    if (VMSVGA3DSURFACE_HAS_HW_SURFACE(pSurface))
    {
        PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
        AssertReturn(pSvgaR3State->pFuncsMap, VERR_NOT_IMPLEMENTED);
        return pSvgaR3State->pFuncsMap->pfnSurfaceUnmap(pThisCC, pImage, pMap, fWritten);
    }

    PVMSVGA3DMIPMAPLEVEL pMipLevel;
    rc = vmsvga3dMipmapLevel(pSurface, pImage->face, pImage->mipmap, &pMipLevel);
    ASSERT_GUEST_RETURN(RT_SUCCESS(rc), rc);

    if (   fWritten
        && (   pMap->enmMapType == VMSVGA3D_SURFACE_MAP_WRITE
            || pMap->enmMapType == VMSVGA3D_SURFACE_MAP_READ_WRITE
            || pMap->enmMapType == VMSVGA3D_SURFACE_MAP_WRITE_DISCARD))
    {
        pMipLevel->fDirty = true;
        pSurface->fDirty = true;
    }

    return VINF_SUCCESS;
}


int vmsvga3dCalcSurfaceMipmapAndFace(PVGASTATECC pThisCC, uint32_t sid, uint32_t iSubresource, uint32_t *piMipmap, uint32_t *piFace)
{
    PVMSVGA3DSURFACE pSurface;
    int rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, sid, &pSurface);
    AssertRCReturn(rc, rc);

    vmsvga3dCalcMipmapAndFace(pSurface->cLevels, iSubresource, piMipmap, piFace);
    return VINF_SUCCESS;
}


uint32_t vmsvga3dCalcSubresourceOffset(PVGASTATECC pThisCC, SVGA3dSurfaceImageId const *pImage)
{
    PVMSVGA3DSURFACE pSurface;
    int rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, pImage->sid, &pSurface);
    AssertRCReturn(rc, 0);

    ASSERT_GUEST_RETURN(pImage->face < pSurface->surfaceDesc.numArrayElements, 0);

    uint32_t offMipLevel = 0;
    for (uint32_t i = 0; i < pImage->mipmap; ++i)
    {
        PVMSVGA3DMIPMAPLEVEL pMipmapLevel = &pSurface->paMipmapLevels[i];
        offMipLevel += pMipmapLevel->cbSurface;
    }

    uint32_t offSubresource = pSurface->surfaceDesc.cbArrayElement * pImage->face + offMipLevel;
    /** @todo Multisample?  */
    return offSubresource;
}


uint32_t vmsvga3dGetArrayElements(PVGASTATECC pThisCC, SVGA3dSurfaceId sid)
{
    PVMSVGA3DSURFACE pSurface;
    int rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, sid, &pSurface);
    AssertRCReturn(rc, 0);

    return pSurface->surfaceDesc.numArrayElements;
}


uint32_t vmsvga3dGetSubresourceCount(PVGASTATECC pThisCC, SVGA3dSurfaceId sid)
{
    PVMSVGA3DSURFACE pSurface;
    int rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, sid, &pSurface);
    AssertRCReturn(rc, 0);

    return pSurface->surfaceDesc.numArrayElements * pSurface->cLevels;
}


bool vmsvga3dIsMultisampleSurface(PVGASTATECC pThisCC, SVGA3dSurfaceId sid)
{
    PVMSVGA3DSURFACE pSurface;
    int rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, sid, &pSurface);
    AssertRCReturn(rc, 0);

    return pSurface->surfaceDesc.multisampleCount > 1;
}


bool vmsvga3dIsEntireImage(PVGASTATECC pThisCC,  SVGA3dSurfaceImageId const *pImage, SVGA3dBox const *pBox)
{
    if (!pBox)
        return true;

    PVMSVGA3DSURFACE pSurface;
    int rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, pImage->sid, &pSurface);
    AssertRCReturn(rc, false);

    PVMSVGA3DMIPMAPLEVEL pMipLevel;
    rc = vmsvga3dMipmapLevel(pSurface, pImage->face, pImage->mipmap, &pMipLevel);
    ASSERT_GUEST_RETURN(RT_SUCCESS(rc), false);

    return (   pBox->x == 0
            && pBox->y == 0
            && pBox->z == 0
            && pBox->w == pMipLevel->mipmapSize.width
            && pBox->h == pMipLevel->mipmapSize.height
            && pBox->d == pMipLevel->mipmapSize.depth);

}


/*
 * Calculates memory layout of a surface box for memcpy:
 */
int vmsvga3dGetBoxDimensions(PVGASTATECC pThisCC, SVGA3dSurfaceImageId const *pImage, SVGA3dBox const *pBox,
                             VMSGA3D_BOX_DIMENSIONS *pResult)
{
    PVMSVGA3DSURFACE pSurface;
    int rc = vmsvga3dSurfaceFromSid(pThisCC->svga.p3dState, pImage->sid, &pSurface);
    AssertRCReturn(rc, rc);

    PVMSVGA3DMIPMAPLEVEL pMipLevel;
    rc = vmsvga3dMipmapLevel(pSurface, pImage->face, pImage->mipmap, &pMipLevel);
    ASSERT_GUEST_RETURN(RT_SUCCESS(rc), rc);

    /* Clip the box. */
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

    uint32_t const cBlocksX = (clipBox.w + pSurface->cxBlock - 1) / pSurface->cxBlock;
    uint32_t const cBlocksY = (clipBox.h + pSurface->cyBlock - 1) / pSurface->cyBlock;

    pResult->offSubresource = vmsvga3dCalcSubresourceOffset(pThisCC, pImage);
    pResult->offBox   = (clipBox.x / pSurface->cxBlock) * pSurface->cbPitchBlock
                      + (clipBox.y / pSurface->cyBlock) * pMipLevel->cbSurfacePitch
                      + clipBox.z * pMipLevel->cbSurfacePlane;
    pResult->cbRow    = cBlocksX * pSurface->cbPitchBlock;
    pResult->cbPitch  = pMipLevel->cbSurfacePitch;
    pResult->cyBlocks = cBlocksY;
    pResult->cRows    = (cBlocksY * pSurface->cbBlock) / pSurface->cbPitchBlock;
    pResult->cbDepthPitch = pMipLevel->cbSurfacePlane;

    return VINF_SUCCESS;
}


/*
 * Whether a legacy 3D backend is used.
 * The new DX context can be built together with the legacy D3D9 or OpenGL backend.
 * The actual backend is selected at the VM startup.
 */
bool vmsvga3dIsLegacyBackend(PVGASTATECC pThisCC)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    return pSvgaR3State->pFuncsDX == NULL;
}


void vmsvga3dReset(PVGASTATECC pThisCC)
{
    /* Deal with data from PVMSVGA3DSTATE */
    PVMSVGA3DSTATE p3dState = pThisCC->svga.p3dState;
    Assert(pThisCC->svga.p3dState);

    if ((pThisCC->svga.p3dState))
    {
        /* Destroy all leftover surfaces. */
        for (uint32_t i = 0; i < p3dState->cSurfaces; i++)
        {
            if (p3dState->papSurfaces[i]->id != SVGA3D_INVALID_ID)
                vmsvga3dSurfaceDestroy(pThisCC, p3dState->papSurfaces[i]->id);
            RTMemFree(p3dState->papSurfaces[i]);
            p3dState->papSurfaces[i] = NULL;
        }
        RTMemFree(p3dState->papSurfaces);
        p3dState->papSurfaces = NULL;
        p3dState->cSurfaces = 0;

        /* Destroy all leftover contexts. */
        for (uint32_t i = 0; i < p3dState->cContexts; i++)
        {
            if (p3dState->papContexts[i]->id != SVGA3D_INVALID_ID)
                vmsvga3dContextDestroy(pThisCC, p3dState->papContexts[i]->id);
            RTMemFree(p3dState->papContexts[i]);
            p3dState->papContexts[i] = NULL;
        }
        RTMemFree(p3dState->papContexts);
        p3dState->papContexts = NULL;
        p3dState->cContexts = 0;

        if (!vmsvga3dIsLegacyBackend(pThisCC))
        {
            /* Destroy all leftover DX contexts. */
            for (uint32_t i = 0; i < p3dState->cDXContexts; i++)
            {
                if (p3dState->papDXContexts[i]->cid != SVGA3D_INVALID_ID)
                    vmsvga3dDXDestroyContext(pThisCC, p3dState->papDXContexts[i]->cid);
                RTMemFree(p3dState->papDXContexts[i]);
                p3dState->papDXContexts[i] = NULL;
            }
            RTMemFree(p3dState->papDXContexts);
            p3dState->papDXContexts = NULL;
            p3dState->cDXContexts = 0;
        }
    }

    /* Reset the backend. */
    PVMSVGAR3STATE pSvgaR3State = pThisCC->svga.pSvgaR3State;
    if (pSvgaR3State->pFuncs3D && pSvgaR3State->pFuncs3D->pfnReset)
        pSvgaR3State->pFuncs3D->pfnReset(pThisCC);
}


void vmsvga3dTerminate(PVGASTATECC pThisCC)
{
    /* Clean up backend. */
    vmsvga3dReset(pThisCC);

    /* Deal with data from PVMSVGA3DSTATE */
    PVMSVGA3DSTATE p3dState = pThisCC->svga.p3dState;
    AssertReturnVoid(p3dState);

    /* Terminate the backend. */
    PVMSVGAR3STATE pSvgaR3State = pThisCC->svga.pSvgaR3State;
    if (pSvgaR3State->pFuncs3D && pSvgaR3State->pFuncs3D->pfnTerminate)
        pSvgaR3State->pFuncs3D->pfnTerminate(pThisCC);

    RTMemFree(p3dState->pBackend);
    p3dState->pBackend = NULL;

    RTMemFree(p3dState);
    pThisCC->svga.p3dState = NULL;
}


int vmsvga3dInit(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC)
{
    PVMSVGAR3STATE pSvgaR3State = pThisCC->svga.pSvgaR3State;

    if (!pThis->svga.fVMSVGA2dGBO)
    {
        /* 3D interface is required. */
        AssertReturn(pSvgaR3State->pFuncs3D && pSvgaR3State->pFuncs3D->pfnInit, VERR_NOT_SUPPORTED);
    }

    PVMSVGA3DSTATE p3dState = (PVMSVGA3DSTATE)RTMemAllocZ(sizeof(VMSVGA3DSTATE));
    AssertReturn(p3dState, VERR_NO_MEMORY);
    pThisCC->svga.p3dState = p3dState;

    p3dState->fVMSVGA2dGBO = pThis->svga.fVMSVGA2dGBO;
    if (pThis->svga.fVMSVGA2dGBO)
        return VINF_SUCCESS;

    int rc = pSvgaR3State->pFuncs3D->pfnInit(pDevIns, pThis, pThisCC);
    if (RT_SUCCESS(rc))
        return VINF_SUCCESS;

    pThisCC->svga.p3dState = NULL;
    RTMemFree(p3dState);
    return rc;
}
