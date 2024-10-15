/* $Id: DevVGA-SVGA3d-savedstate.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevSVGA3d - VMWare SVGA device, 3D parts - Saved state and assocated stuff.
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



/**
 * Reinitializes an active context.
 *
 * @returns VBox status code.
 * @param   pThisCC         The VGA/VMSVGA state for ring-3.
 * @param   pContext        The freshly loaded context to reinitialize.
 */
static int vmsvga3dLoadReinitContext(PVGASTATECC pThisCC, PVMSVGA3DCONTEXT pContext)
{
    int      rc;
    uint32_t cid = pContext->id;
    Assert(cid != SVGA3D_INVALID_ID);

    /* First set the render targets as they change the internal state (reset viewport etc) */
    Log(("vmsvga3dLoadReinitContext: Recreate render targets BEGIN [cid=%#x]\n", cid));
    for (uint32_t j = 0; j < RT_ELEMENTS(pContext->state.aRenderTargets); j++)
    {
        if (pContext->state.aRenderTargets[j] != SVGA3D_INVALID_ID)
        {
            SVGA3dSurfaceImageId target;

            target.sid      = pContext->state.aRenderTargets[j];
            target.face     = 0;
            target.mipmap   = 0;
            rc = vmsvga3dSetRenderTarget(pThisCC, cid, (SVGA3dRenderTargetType)j, target);
            AssertRCReturn(rc, rc);
        }
    }
    Log(("vmsvga3dLoadReinitContext: Recreate render targets END\n"));

    /* Recreate the render state */
    Log(("vmsvga3dLoadReinitContext: Recreate render state BEGIN\n"));
    for (uint32_t j = 0; j < RT_ELEMENTS(pContext->state.aRenderState); j++)
    {
        SVGA3dRenderState *pRenderState = &pContext->state.aRenderState[j];

        if (pRenderState->state != SVGA3D_RS_INVALID)
            vmsvga3dSetRenderState(pThisCC, pContext->id, 1, pRenderState);
    }
    Log(("vmsvga3dLoadReinitContext: Recreate render state END\n"));

    /* Recreate the texture state */
    Log(("vmsvga3dLoadReinitContext: Recreate texture state BEGIN\n"));
    for (uint32_t iStage = 0; iStage < RT_ELEMENTS(pContext->state.aTextureStates); ++iStage)
    {
        for (uint32_t j = 0; j < RT_ELEMENTS(pContext->state.aTextureStates[0]); ++j)
        {
            SVGA3dTextureState *pTextureState = &pContext->state.aTextureStates[iStage][j];

            if (pTextureState->name != SVGA3D_TS_INVALID)
                vmsvga3dSetTextureState(pThisCC, pContext->id, 1, pTextureState);
        }
    }
    Log(("vmsvga3dLoadReinitContext: Recreate texture state END\n"));

    /* Reprogram the clip planes. */
    for (uint32_t j = 0; j < RT_ELEMENTS(pContext->state.aClipPlane); j++)
    {
        if (pContext->state.aClipPlane[j].fValid == true)
            vmsvga3dSetClipPlane(pThisCC, cid, j, pContext->state.aClipPlane[j].plane);
    }

    /* Reprogram the light data. */
    for (uint32_t j = 0; j < RT_ELEMENTS(pContext->state.aLightData); j++)
    {
        if (pContext->state.aLightData[j].fValidData == true)
            vmsvga3dSetLightData(pThisCC, cid, j, &pContext->state.aLightData[j].data);
        if (pContext->state.aLightData[j].fEnabled)
            vmsvga3dSetLightEnabled(pThisCC, cid, j, true);
    }

    /* Recreate the transform state. */
    if (pContext->state.u32UpdateFlags & VMSVGA3D_UPDATE_TRANSFORM)
    {
        for (uint32_t j = 0; j < RT_ELEMENTS(pContext->state.aTransformState); j++)
        {
            if (pContext->state.aTransformState[j].fValid == true)
                vmsvga3dSetTransform(pThisCC, cid, (SVGA3dTransformType)j, pContext->state.aTransformState[j].matrix);
        }
    }

    /* Reprogram the material data. */
    if (pContext->state.u32UpdateFlags & VMSVGA3D_UPDATE_MATERIAL)
    {
        for (uint32_t j = 0; j < RT_ELEMENTS(pContext->state.aMaterial); j++)
        {
            if (pContext->state.aMaterial[j].fValid == true)
                vmsvga3dSetMaterial(pThisCC, cid, (SVGA3dFace)j, &pContext->state.aMaterial[j].material);
        }
    }

    if (pContext->state.u32UpdateFlags & VMSVGA3D_UPDATE_SCISSORRECT)
        vmsvga3dSetScissorRect(pThisCC, cid, &pContext->state.RectScissor);
    if (pContext->state.u32UpdateFlags & VMSVGA3D_UPDATE_ZRANGE)
        vmsvga3dSetZRange(pThisCC, cid, pContext->state.zRange);
    if (pContext->state.u32UpdateFlags & VMSVGA3D_UPDATE_VIEWPORT)
        vmsvga3dSetViewPort(pThisCC, cid, &pContext->state.RectViewPort);
    if (pContext->state.u32UpdateFlags & VMSVGA3D_UPDATE_VERTEXSHADER)
        vmsvga3dShaderSet(pThisCC, pContext, cid, SVGA3D_SHADERTYPE_VS, pContext->state.shidVertex);
    if (pContext->state.u32UpdateFlags & VMSVGA3D_UPDATE_PIXELSHADER)
        vmsvga3dShaderSet(pThisCC, pContext, cid, SVGA3D_SHADERTYPE_PS, pContext->state.shidPixel);

    Log(("vmsvga3dLoadReinitContext: returns [cid=%#x]\n", cid));
    return VINF_SUCCESS;
}

static int vmsvga3dLoadVMSVGA3DSURFACEPreMipLevels(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, VMSVGA3DSURFACE *pSurface)
{
    struct VMSVGA3DSURFACEPreMipLevels
    {
        uint32_t            id;
        uint32_t            idAssociatedContext;
        uint32_t            surfaceFlags;
        SVGA3dSurfaceFormat format;
#ifdef VMSVGA3D_OPENGL
        GLint               internalFormatGL;
        GLint               formatGL;
        GLint               typeGL;
#endif
        SVGA3dSurfaceFace   faces[SVGA3D_MAX_SURFACE_FACES];
        uint32_t            cFaces;
        uint32_t            multiSampleCount;
        SVGA3dTextureFilter autogenFilter;
        uint32_t            cbBlock;
    };

    static SSMFIELD const s_aVMSVGA3DSURFACEFieldsPreMipLevels[] =
    {
        SSMFIELD_ENTRY(struct VMSVGA3DSURFACEPreMipLevels, id),
        SSMFIELD_ENTRY(struct VMSVGA3DSURFACEPreMipLevels, idAssociatedContext),
        SSMFIELD_ENTRY(struct VMSVGA3DSURFACEPreMipLevels, surfaceFlags),
        SSMFIELD_ENTRY(struct VMSVGA3DSURFACEPreMipLevels, format),
#ifdef VMSVGA3D_OPENGL
        SSMFIELD_ENTRY(struct VMSVGA3DSURFACEPreMipLevels, internalFormatGL),
        SSMFIELD_ENTRY(struct VMSVGA3DSURFACEPreMipLevels, formatGL),
        SSMFIELD_ENTRY(struct VMSVGA3DSURFACEPreMipLevels, typeGL),
#endif
        SSMFIELD_ENTRY(struct VMSVGA3DSURFACEPreMipLevels, faces),
        SSMFIELD_ENTRY(struct VMSVGA3DSURFACEPreMipLevels, cFaces),
        SSMFIELD_ENTRY(struct VMSVGA3DSURFACEPreMipLevels, multiSampleCount),
        SSMFIELD_ENTRY(struct VMSVGA3DSURFACEPreMipLevels, autogenFilter),
#ifdef VMSVGA3D_DIRECT3D
        SSMFIELD_ENTRY(struct VMSVGA3DSURFACEPreMipLevels, format), /* Yes, the 'format' field is duplicated. */
#endif
        SSMFIELD_ENTRY(struct VMSVGA3DSURFACEPreMipLevels, cbBlock),
        SSMFIELD_ENTRY_TERM()
    };

    struct VMSVGA3DSURFACEPreMipLevels surfacePreMipLevels;
    int rc = pDevIns->pHlpR3->pfnSSMGetStructEx(pSSM, &surfacePreMipLevels, sizeof(surfacePreMipLevels), 0, s_aVMSVGA3DSURFACEFieldsPreMipLevels, NULL);
    if (RT_SUCCESS(rc))
    {
        pSurface->id                       = surfacePreMipLevels.id;
        pSurface->idAssociatedContext      = surfacePreMipLevels.idAssociatedContext;
        pSurface->f.s.surface1Flags        = surfacePreMipLevels.surfaceFlags;
        pSurface->f.s.surface2Flags        = 0;
        pSurface->format                   = surfacePreMipLevels.format;
#ifdef VMSVGA3D_OPENGL
        pSurface->internalFormatGL         = surfacePreMipLevels.internalFormatGL;
        pSurface->formatGL                 = surfacePreMipLevels.formatGL;
        pSurface->typeGL                   = surfacePreMipLevels.typeGL;
#endif
        pSurface->cLevels                  = surfacePreMipLevels.faces[0].numMipLevels;
        pSurface->cFaces                   = surfacePreMipLevels.cFaces;
        pSurface->multiSampleCount         = surfacePreMipLevels.multiSampleCount;
        pSurface->autogenFilter            = surfacePreMipLevels.autogenFilter;
        pSurface->cbBlock                  = surfacePreMipLevels.cbBlock;
    }
    return rc;
}


/*
 * Load the legacy VMSVGA3DCONTEXT from saved state version VGA_SAVEDSTATE_VERSION_VMSVGA_MIPLEVELS (23)
 * or earlier, i.e. 6.1 or old trunk.
 *
 * The saved state incompatibility has been introduced in two revisions:
 *
 * - r140506: which makes sure that VMSVGA structures are tightly packed (pragma pack(1)).
 *   This caused all structures which have a member from VMSVGA headers (like VMSVGALIGHTSTATE) to be packed too.
 *   For example the size of aLightData element (VMSVGALIGHTSTATE) is 2 bytes smaller on trunk (118) than on 6.1 (120),
 *   which happens because SVGA3dLightData member offset is 2 on trunk and 4 on 6.1.
 *
 * - r141385: new VMSVGA device headers.
 *   SVGA3D_RS_MAX is 99 with new VMSVGA headers, but it was 100 with old headers.
 *   6.1 always saved 100 entries; trunk before r141385 saved 100 entries; trunk at r141385 saves 99 entries.
 *
 *   6.1 saved state version is VGA_SAVEDSTATE_VERSION_VMSVGA_SCREENS (21).
 *   Trunk r141287 introduced VGA_SAVEDSTATE_VERSION_VMSVGA_MIPLEVELS (23).
 *
 * Both issues has been solved by loading a compatible context structure for saved state
 * version < VGA_SAVEDSTATE_VERSION_VMSVGA_MIPLEVELS.
 * This means that trunk will not be able to load states created from r140506 to r141385.
 */
static int vmsvga3dLoadVMSVGA3DCONTEXT23(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, VMSVGA3DCONTEXT *pContext)
{
#pragma pack(1)
    struct VMSVGA3DCONTEXT23
    {
        uint32_t                id;
#ifdef VMSVGA3D_OPENGL
        uint32_t                lastError;
#endif
        uint32_t                cPixelShaders;
        uint32_t                cVertexShaders;
        struct
        {
            uint32_t            u32UpdateFlags;
            SVGA3dRenderState   aRenderState[/*SVGA3D_RS_MAX=*/ 100];
            SVGA3dTextureState  aTextureStates[/*SVGA3D_MAX_TEXTURE_STAGE=*/ 8][/*SVGA3D_TS_MAX=*/ 30];
            struct
            {
                bool            fValid;
                bool            pad[3];
                float           matrix[16];
            } aTransformState[SVGA3D_TRANSFORM_MAX];
            struct
            {
                bool            fValid;
                bool            pad[3];
                SVGA3dMaterial  material;
            } aMaterial[SVGA3D_FACE_MAX];
            struct
            {
                bool            fValid;
                bool            pad[3];
                float           plane[4];
            } aClipPlane[SVGA3D_CLIPPLANE_5];
            struct
            {
                bool            fEnabled;
                bool            fValidData;
                bool            pad[2];
                SVGA3dLightData data;
            } aLightData[SVGA3D_MAX_LIGHTS];
            uint32_t            aRenderTargets[SVGA3D_RT_MAX];
            SVGA3dRect          RectScissor;
            SVGA3dRect          RectViewPort;
            SVGA3dZRange        zRange;
            uint32_t            shidPixel;
            uint32_t            shidVertex;
            uint32_t            cPixelShaderConst;
            uint32_t            cVertexShaderConst;
        } state;
    };
#pragma pack()

    static SSMFIELD const g_aVMSVGA3DCONTEXT23Fields[] =
    {
        SSMFIELD_ENTRY(VMSVGA3DCONTEXT23, id),
#ifdef VMSVGA3D_OPENGL
        SSMFIELD_ENTRY(VMSVGA3DCONTEXT23, lastError),
#endif
        SSMFIELD_ENTRY(VMSVGA3DCONTEXT23, cPixelShaders),
        SSMFIELD_ENTRY(VMSVGA3DCONTEXT23, cVertexShaders),
        SSMFIELD_ENTRY(VMSVGA3DCONTEXT23, state.u32UpdateFlags),
        SSMFIELD_ENTRY(VMSVGA3DCONTEXT23, state.aRenderState),
        SSMFIELD_ENTRY(VMSVGA3DCONTEXT23, state.aTextureStates),
        SSMFIELD_ENTRY(VMSVGA3DCONTEXT23, state.aTransformState),
        SSMFIELD_ENTRY(VMSVGA3DCONTEXT23, state.aMaterial),
        SSMFIELD_ENTRY(VMSVGA3DCONTEXT23, state.aClipPlane),
        SSMFIELD_ENTRY(VMSVGA3DCONTEXT23, state.aLightData),
        SSMFIELD_ENTRY(VMSVGA3DCONTEXT23, state.aRenderTargets),
        SSMFIELD_ENTRY(VMSVGA3DCONTEXT23, state.RectScissor),
        SSMFIELD_ENTRY(VMSVGA3DCONTEXT23, state.RectViewPort),
        SSMFIELD_ENTRY(VMSVGA3DCONTEXT23, state.zRange),
        SSMFIELD_ENTRY(VMSVGA3DCONTEXT23, state.shidPixel),
        SSMFIELD_ENTRY(VMSVGA3DCONTEXT23, state.shidVertex),
        SSMFIELD_ENTRY(VMSVGA3DCONTEXT23, state.cPixelShaderConst),
        SSMFIELD_ENTRY(VMSVGA3DCONTEXT23, state.cVertexShaderConst),
        SSMFIELD_ENTRY_TERM()
    };

    struct VMSVGA3DCONTEXT23 ctx;
    int rc = pDevIns->pHlpR3->pfnSSMGetStructEx(pSSM, &ctx, sizeof(ctx), 0, g_aVMSVGA3DCONTEXT23Fields, NULL);
    AssertRCReturn(rc, rc);

    pContext->id                       = ctx.id;
#ifdef VMSVGA3D_OPENGL
    pContext->lastError                = (GLenum)ctx.lastError;
#endif

    pContext->cPixelShaders            = ctx.cPixelShaders;
    pContext->cVertexShaders           = ctx.cVertexShaders;
    pContext->state.u32UpdateFlags     = ctx.state.u32UpdateFlags;

    AssertCompile(sizeof(SVGA3dRenderState) == 8);
    AssertCompile(RT_ELEMENTS(pContext->state.aRenderState) == 99);
    for (unsigned i = 0; i < RT_ELEMENTS(pContext->state.aRenderState); ++i)
        pContext->state.aRenderState[i] = ctx.state.aRenderState[i];

    // Skip pContext->state.aTextureStates
    AssertCompile(sizeof(SVGA3dTextureState) == 12);

    AssertCompile(sizeof(VMSVGATRANSFORMSTATE) == 68);
    AssertCompile(RT_ELEMENTS(pContext->state.aTransformState) == 15);
    for (unsigned i = 0; i < RT_ELEMENTS(pContext->state.aTransformState); ++i)
    {
        pContext->state.aTransformState[i].fValid = ctx.state.aTransformState[i].fValid;
        memcpy(pContext->state.aTransformState[i].matrix, ctx.state.aTransformState[i].matrix, sizeof(pContext->state.aTransformState[i].matrix));
    }

    AssertCompile(sizeof(SVGA3dMaterial) == 68);
    AssertCompile(RT_ELEMENTS(pContext->state.aMaterial) == 5);
    for (unsigned i = 0; i < RT_ELEMENTS(pContext->state.aMaterial); ++i)
    {
        pContext->state.aMaterial[i].fValid = ctx.state.aMaterial[i].fValid;
        pContext->state.aMaterial[i].material = ctx.state.aMaterial[i].material;
    }

    AssertCompile(sizeof(VMSVGACLIPPLANESTATE) == 20);
    AssertCompile(RT_ELEMENTS(pContext->state.aClipPlane) == (1 << 5));
    for (unsigned i = 0; i < RT_ELEMENTS(pContext->state.aClipPlane); ++i)
    {
        pContext->state.aClipPlane[i].fValid = ctx.state.aClipPlane[i].fValid;
        memcpy(pContext->state.aClipPlane[i].plane, ctx.state.aClipPlane[i].plane, sizeof(pContext->state.aClipPlane[i].plane));
    }

    AssertCompile(sizeof(SVGA3dLightData) == 116);
    AssertCompile(RT_ELEMENTS(pContext->state.aLightData) == 32);
    for (unsigned i = 0; i < RT_ELEMENTS(pContext->state.aLightData); ++i)
    {
        pContext->state.aLightData[i].fEnabled = ctx.state.aLightData[i].fEnabled;
        pContext->state.aLightData[i].fValidData = ctx.state.aLightData[i].fValidData;
        pContext->state.aLightData[i].data = ctx.state.aLightData[i].data;
    }

    AssertCompile(RT_ELEMENTS(pContext->state.aRenderTargets) == 10);
    memcpy(pContext->state.aRenderTargets, ctx.state.aRenderTargets, SVGA3D_RT_MAX * sizeof(uint32_t));

    AssertCompile(sizeof(SVGA3dRect) == 16);
    pContext->state.RectScissor        = ctx.state.RectScissor;
    pContext->state.RectViewPort       = ctx.state.RectViewPort;

    AssertCompile(sizeof(SVGA3dZRange) == 8);
    pContext->state.zRange             = ctx.state.zRange;

    pContext->state.shidPixel          = ctx.state.shidPixel;
    pContext->state.shidVertex         = ctx.state.shidVertex;
    pContext->state.cPixelShaderConst  = ctx.state.cPixelShaderConst;
    pContext->state.cVertexShaderConst = ctx.state.cVertexShaderConst;

    return VINF_SUCCESS;
}

int vmsvga3dLoadExec(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    RT_NOREF(pDevIns, pThis, uPass);
    PVMSVGA3DSTATE  pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_NO_MEMORY);
    PCPDMDEVHLPR3   pHlp = pDevIns->pHlpR3;
    int             rc;
    uint32_t       cContexts, cSurfaces;
    LogFlow(("vmsvga3dLoadExec:\n"));

    /* Get the generic 3d state first. */
    rc = pHlp->pfnSSMGetStructEx(pSSM, pState, sizeof(*pState), 0, g_aVMSVGA3DSTATEFields, NULL);
    AssertRCReturn(rc, rc);

    cContexts                           = pState->cContexts;
    cSurfaces                           = pState->cSurfaces;
    pState->cContexts                   = 0;
    pState->cSurfaces                   = 0;

    /* Fetch all active contexts. */
    for (uint32_t i = 0; i < cContexts; i++)
    {
        PVMSVGA3DCONTEXT pContext;
        uint32_t         cid;

        /* Get the context id */
        rc = pHlp->pfnSSMGetU32(pSSM, &cid);
        AssertRCReturn(rc, rc);

        if (cid != SVGA3D_INVALID_ID)
        {
            uint32_t cPixelShaderConst, cVertexShaderConst, cPixelShaders, cVertexShaders;
            LogFlow(("vmsvga3dLoadExec: Loading cid=%#x\n", cid));

#ifdef VMSVGA3D_OPENGL
            if (cid == VMSVGA3D_SHARED_CTX_ID)
            {
                i--; /* Not included in cContexts. */
                pContext = &pState->SharedCtx;
                if (pContext->id != VMSVGA3D_SHARED_CTX_ID)
                {
                    /** @todo Separate backend */
                    rc = vmsvga3dContextDefineOgl(pThisCC, VMSVGA3D_SHARED_CTX_ID, VMSVGA3D_DEF_CTX_F_SHARED_CTX);
                    AssertRCReturn(rc, rc);
                }
            }
            else
#endif
            {
                rc = vmsvga3dContextDefine(pThisCC, cid);
                AssertRCReturn(rc, rc);

                pContext = pState->papContexts[i];
            }
            AssertReturn(pContext->id == cid, VERR_INTERNAL_ERROR);

            if (uVersion >= VGA_SAVEDSTATE_VERSION_VMSVGA_MIPLEVELS)
                rc = pHlp->pfnSSMGetStructEx(pSSM, pContext, sizeof(*pContext), 0, g_aVMSVGA3DCONTEXTFields, NULL);
            else
                rc = vmsvga3dLoadVMSVGA3DCONTEXT23(pDevIns, pSSM, pContext);
            AssertRCReturn(rc, rc);

            cPixelShaders                       = pContext->cPixelShaders;
            cVertexShaders                      = pContext->cVertexShaders;
            cPixelShaderConst                   = pContext->state.cPixelShaderConst;
            cVertexShaderConst                  = pContext->state.cVertexShaderConst;
            pContext->cPixelShaders             = 0;
            pContext->cVertexShaders            = 0;
            pContext->state.cPixelShaderConst   = 0;
            pContext->state.cVertexShaderConst  = 0;

            /* Fetch all pixel shaders. */
            for (uint32_t j = 0; j < cPixelShaders; j++)
            {
                VMSVGA3DSHADER  shader;
                uint32_t        shid;

                /* Fetch the id first. */
                rc = pHlp->pfnSSMGetU32(pSSM, &shid);
                AssertRCReturn(rc, rc);

                if (shid != SVGA3D_INVALID_ID)
                {
                    uint32_t *pData;

                    /* Fetch a copy of the shader struct. */
                    rc = pHlp->pfnSSMGetStructEx(pSSM, &shader, sizeof(shader), 0, g_aVMSVGA3DSHADERFields, NULL);
                    AssertRCReturn(rc, rc);

                    pData = (uint32_t *)RTMemAlloc(shader.cbData);
                    AssertReturn(pData, VERR_NO_MEMORY);

                    rc = pHlp->pfnSSMGetMem(pSSM, pData, shader.cbData);
                    AssertRCReturn(rc, rc);

                    rc = vmsvga3dShaderDefine(pThisCC, cid, shid, shader.type, shader.cbData, pData);
                    AssertRCReturn(rc, rc);

                    RTMemFree(pData);
                }
            }

            /* Fetch all vertex shaders. */
            for (uint32_t j = 0; j < cVertexShaders; j++)
            {
                VMSVGA3DSHADER  shader;
                uint32_t        shid;

                /* Fetch the id first. */
                rc = pHlp->pfnSSMGetU32(pSSM, &shid);
                AssertRCReturn(rc, rc);

                if (shid != SVGA3D_INVALID_ID)
                {
                    uint32_t *pData;

                    /* Fetch a copy of the shader struct. */
                    rc = pHlp->pfnSSMGetStructEx(pSSM, &shader, sizeof(shader), 0, g_aVMSVGA3DSHADERFields, NULL);
                    AssertRCReturn(rc, rc);

                    pData = (uint32_t *)RTMemAlloc(shader.cbData);
                    AssertReturn(pData, VERR_NO_MEMORY);

                    rc = pHlp->pfnSSMGetMem(pSSM, pData, shader.cbData);
                    AssertRCReturn(rc, rc);

                    rc = vmsvga3dShaderDefine(pThisCC, cid, shid, shader.type, shader.cbData, pData);
                    AssertRCReturn(rc, rc);

                    RTMemFree(pData);
                }
            }

            /* Fetch pixel shader constants. */
            for (uint32_t j = 0; j < cPixelShaderConst; j++)
            {
                VMSVGASHADERCONST ShaderConst;

                rc = pHlp->pfnSSMGetStructEx(pSSM, &ShaderConst, sizeof(ShaderConst), 0, g_aVMSVGASHADERCONSTFields, NULL);
                AssertRCReturn(rc, rc);

                if (ShaderConst.fValid)
                {
                    rc = vmsvga3dShaderSetConst(pThisCC, cid, j, SVGA3D_SHADERTYPE_PS, ShaderConst.ctype, 1, ShaderConst.value);
                    AssertRCReturn(rc, rc);
                }
            }

            /* Fetch vertex shader constants. */
            for (uint32_t j = 0; j < cVertexShaderConst; j++)
            {
                VMSVGASHADERCONST ShaderConst;

                rc = pHlp->pfnSSMGetStructEx(pSSM, &ShaderConst, sizeof(ShaderConst), 0, g_aVMSVGASHADERCONSTFields, NULL);
                AssertRCReturn(rc, rc);

                if (ShaderConst.fValid)
                {
                    rc = vmsvga3dShaderSetConst(pThisCC, cid, j, SVGA3D_SHADERTYPE_VS, ShaderConst.ctype, 1, ShaderConst.value);
                    AssertRCReturn(rc, rc);
                }
            }

            if (uVersion >= VGA_SAVEDSTATE_VERSION_VMSVGA_TEX_STAGES)
            {
                /* Load texture stage and samplers state. */

                /* Number of stages/samplers. */
                uint32_t cStages;
                rc = pHlp->pfnSSMGetU32(pSSM, &cStages);
                AssertRCReturn(rc, rc);

                /* Number of states. */
                uint32_t cTextureStates;
                rc = pHlp->pfnSSMGetU32(pSSM, &cTextureStates);
                AssertRCReturn(rc, rc);

                for (uint32_t iStage = 0; iStage < cStages; ++iStage)
                {
                    for (uint32_t j = 0; j < cTextureStates; ++j)
                    {
                        SVGA3dTextureState textureState;
                        pHlp->pfnSSMGetU32(pSSM, &textureState.stage);
                        uint32_t u32Name;
                        pHlp->pfnSSMGetU32(pSSM, &u32Name);
                        textureState.name = (SVGA3dTextureStateName)u32Name;
                        rc = pHlp->pfnSSMGetU32(pSSM, &textureState.value);
                        AssertRCReturn(rc, rc);

                        if (   iStage < RT_ELEMENTS(pContext->state.aTextureStates)
                            && j < RT_ELEMENTS(pContext->state.aTextureStates[0]))
                        {
                            pContext->state.aTextureStates[iStage][j] = textureState;
                        }
                    }
                }
            }

            if (uVersion >= VGA_SAVEDSTATE_VERSION_VMSVGA)
            {
                VMSVGA3DQUERY query;
                RT_ZERO(query);

                rc = pHlp->pfnSSMGetStructEx(pSSM, &query, sizeof(query), 0, g_aVMSVGA3DQUERYFields, NULL);
                AssertRCReturn(rc, rc);

                switch (query.enmQueryState)
                {
                    case VMSVGA3DQUERYSTATE_BUILDING:
                        /* Start collecting data. */
                        vmsvga3dQueryBegin(pThisCC, cid, SVGA3D_QUERYTYPE_OCCLUSION);
                        /* Partial result. */
                        pContext->occlusion.u32QueryResult = query.u32QueryResult;
                        break;

                    case VMSVGA3DQUERYSTATE_ISSUED:
                        /* Guest ended the query but did not read result. Result is restored. */
                        query.enmQueryState = VMSVGA3DQUERYSTATE_SIGNALED;
                        RT_FALL_THRU();
                    case VMSVGA3DQUERYSTATE_SIGNALED:
                        /* Create the query object. */
                        vmsvga3dQueryCreate(pThisCC, cid, SVGA3D_QUERYTYPE_OCCLUSION);

                        /* Update result and state. */
                        pContext->occlusion.enmQueryState = query.enmQueryState;
                        pContext->occlusion.u32QueryResult = query.u32QueryResult;
                        break;

                    default:
                        AssertFailed();
                        RT_FALL_THRU();
                    case VMSVGA3DQUERYSTATE_NULL:
                        RT_ZERO(pContext->occlusion);
                        break;
                }
            }
        }
    }

#ifdef VMSVGA3D_OPENGL
    /* Make the shared context the current one. */
    if (pState->SharedCtx.id == VMSVGA3D_SHARED_CTX_ID)
        VMSVGA3D_SET_CURRENT_CONTEXT(pState, &pState->SharedCtx);
#endif

    /* Fetch all surfaces. */
    for (uint32_t i = 0; i < cSurfaces; i++)
    {
        uint32_t sid;

        /* Fetch the id first. */
        rc = pHlp->pfnSSMGetU32(pSSM, &sid);
        AssertRCReturn(rc, rc);

        if (sid != SVGA3D_INVALID_ID)
        {
            VMSVGA3DSURFACE  surface;
            LogFlow(("vmsvga3dLoadExec: Loading sid=%#x\n", sid));

            /* Fetch the surface structure first. */
            if (RT_LIKELY(uVersion >= VGA_SAVEDSTATE_VERSION_VMSVGA_MIPLEVELS))
                rc = pHlp->pfnSSMGetStructEx(pSSM, &surface, sizeof(surface), 0, g_aVMSVGA3DSURFACEFields, NULL);
            else
                rc = vmsvga3dLoadVMSVGA3DSURFACEPreMipLevels(pDevIns, pSSM, &surface);
            AssertRCReturn(rc, rc);

            {
                uint32_t             cMipLevels = surface.cLevels * surface.cFaces;
                PVMSVGA3DMIPMAPLEVEL pMipmapLevel = (PVMSVGA3DMIPMAPLEVEL)RTMemAlloc(cMipLevels * sizeof(VMSVGA3DMIPMAPLEVEL));
                AssertReturn(pMipmapLevel, VERR_NO_MEMORY);
                SVGA3dSize *pMipmapLevelSize = (SVGA3dSize *)RTMemAlloc(cMipLevels * sizeof(SVGA3dSize));
                AssertReturn(pMipmapLevelSize, VERR_NO_MEMORY);

                /* Load the mip map level info. */
                for (uint32_t face=0; face < surface.cFaces; face++)
                {
                    for (uint32_t j = 0; j < surface.cLevels; j++)
                    {
                        uint32_t idx = j + face * surface.cLevels;
                        /* Load the mip map level struct. */
                        rc = pHlp->pfnSSMGetStructEx(pSSM, &pMipmapLevel[idx], sizeof(pMipmapLevel[idx]), 0,
                                                     g_aVMSVGA3DMIPMAPLEVELFields, NULL);
                        AssertRCReturn(rc, rc);

                        pMipmapLevelSize[idx] = pMipmapLevel[idx].mipmapSize;
                    }
                }

                rc = vmsvga3dSurfaceDefine(pThisCC, sid, surface.f.surfaceFlags, surface.format, surface.multiSampleCount,
                                           SVGA3D_MS_PATTERN_NONE, SVGA3D_MS_QUALITY_NONE,
                                           surface.autogenFilter, surface.cLevels, &pMipmapLevelSize[0], /* arraySize = */ 0, /* bufferByteStride = */ 0, /* fAllocMipLevels = */ true);
                AssertRCReturn(rc, rc);

                RTMemFree(pMipmapLevelSize);
                RTMemFree(pMipmapLevel);
            }

            PVMSVGA3DSURFACE pSurface = pState->papSurfaces[sid];
            Assert(pSurface->id == sid);

            pSurface->fDirty = false;

            /* Load the mip map level data. */
            for (uint32_t j = 0; j < pSurface->cLevels * pSurface->cFaces; j++)
            {
                PVMSVGA3DMIPMAPLEVEL pMipmapLevel = &pSurface->paMipmapLevels[j];
                bool fDataPresent = false;

                /* vmsvga3dSurfaceDefine already allocated the surface data buffer. */
                Assert(pMipmapLevel->cbSurface);
                AssertReturn(pMipmapLevel->pSurfaceData, VERR_INTERNAL_ERROR);

                /* Fetch the data present boolean first. */
                rc = pHlp->pfnSSMGetBool(pSSM, &fDataPresent);
                AssertRCReturn(rc, rc);

                Log(("Surface sid=%u: load mipmap level %d with %x bytes data (present=%d).\n", sid, j, pMipmapLevel->cbSurface, fDataPresent));

                if (fDataPresent)
                {
                    rc = pHlp->pfnSSMGetMem(pSSM, pMipmapLevel->pSurfaceData, pMipmapLevel->cbSurface);
                    AssertRCReturn(rc, rc);
                    pMipmapLevel->fDirty = true;
                    pSurface->fDirty     = true;
                }
                else
                {
                    pMipmapLevel->fDirty = false;
                }
            }
        }
    }

#ifdef VMSVGA3D_OPENGL
    /* Reinitialize the shared context. */
    LogFlow(("vmsvga3dLoadExec: pState->SharedCtx.id=%#x\n", pState->SharedCtx.id));
    if (pState->SharedCtx.id == VMSVGA3D_SHARED_CTX_ID)
    {
        rc = vmsvga3dLoadReinitContext(pThisCC, &pState->SharedCtx);
        AssertRCReturn(rc, rc);
    }
#endif

    /* Reinitialize all active contexts. */
    for (uint32_t i = 0; i < pState->cContexts; i++)
    {
        PVMSVGA3DCONTEXT pContext = pState->papContexts[i];
        if (pContext->id != SVGA3D_INVALID_ID)
        {
            rc = vmsvga3dLoadReinitContext(pThisCC, pContext);
            AssertRCReturn(rc, rc);
        }
    }

    LogFlow(("vmsvga3dLoadExec: return success\n"));
    return VINF_SUCCESS;
}


static int vmsvga3dSaveContext(PCPDMDEVHLPR3 pHlp, PVGASTATECC pThisCC, PSSMHANDLE pSSM, PVMSVGA3DCONTEXT pContext)
{
    uint32_t cid = pContext->id;

    /* Save the id first. */
    int rc = pHlp->pfnSSMPutU32(pSSM, cid);
    AssertRCReturn(rc, rc);

    if (cid != SVGA3D_INVALID_ID)
    {
        /* Save a copy of the context structure first. */
        rc = pHlp->pfnSSMPutStructEx(pSSM, pContext, sizeof(*pContext), 0, g_aVMSVGA3DCONTEXTFields, NULL);
        AssertRCReturn(rc, rc);

        /* Save all pixel shaders. */
        for (uint32_t j = 0; j < pContext->cPixelShaders; j++)
        {
            PVMSVGA3DSHADER pShader = &pContext->paPixelShader[j];

            /* Save the id first. */
            rc = pHlp->pfnSSMPutU32(pSSM, pShader->id);
            AssertRCReturn(rc, rc);

            if (pShader->id != SVGA3D_INVALID_ID)
            {
                uint32_t cbData = pShader->cbData;

                /* Save a copy of the shader struct. */
                rc = pHlp->pfnSSMPutStructEx(pSSM, pShader, sizeof(*pShader), 0, g_aVMSVGA3DSHADERFields, NULL);
                AssertRCReturn(rc, rc);

                Log(("Save pixelshader shid=%d with %x bytes code.\n", pShader->id, cbData));
                rc = pHlp->pfnSSMPutMem(pSSM, pShader->pShaderProgram, cbData);
                AssertRCReturn(rc, rc);
            }
        }

        /* Save all vertex shaders. */
        for (uint32_t j = 0; j < pContext->cVertexShaders; j++)
        {
            PVMSVGA3DSHADER pShader = &pContext->paVertexShader[j];

            /* Save the id first. */
            rc = pHlp->pfnSSMPutU32(pSSM, pShader->id);
            AssertRCReturn(rc, rc);

            if (pShader->id != SVGA3D_INVALID_ID)
            {
                uint32_t cbData = pShader->cbData;

                /* Save a copy of the shader struct. */
                rc = pHlp->pfnSSMPutStructEx(pSSM, pShader, sizeof(*pShader), 0, g_aVMSVGA3DSHADERFields, NULL);
                AssertRCReturn(rc, rc);

                Log(("Save vertex shader shid=%d with %x bytes code.\n", pShader->id, cbData));
                /* Fetch the shader code and save it. */
                rc = pHlp->pfnSSMPutMem(pSSM, pShader->pShaderProgram, cbData);
                AssertRCReturn(rc, rc);
            }
        }

        /* Save pixel shader constants. */
        for (uint32_t j = 0; j < pContext->state.cPixelShaderConst; j++)
        {
            rc = pHlp->pfnSSMPutStructEx(pSSM, &pContext->state.paPixelShaderConst[j], sizeof(pContext->state.paPixelShaderConst[j]), 0, g_aVMSVGASHADERCONSTFields, NULL);
            AssertRCReturn(rc, rc);
        }

        /* Save vertex shader constants. */
        for (uint32_t j = 0; j < pContext->state.cVertexShaderConst; j++)
        {
            rc = pHlp->pfnSSMPutStructEx(pSSM, &pContext->state.paVertexShaderConst[j], sizeof(pContext->state.paVertexShaderConst[j]), 0, g_aVMSVGASHADERCONSTFields, NULL);
            AssertRCReturn(rc, rc);
        }

        /* Save texture stage and samplers state. */

        /* Number of stages/samplers. */
        rc = pHlp->pfnSSMPutU32(pSSM, RT_ELEMENTS(pContext->state.aTextureStates));
        AssertRCReturn(rc, rc);

        /* Number of texture states. */
        rc = pHlp->pfnSSMPutU32(pSSM, RT_ELEMENTS(pContext->state.aTextureStates[0]));
        AssertRCReturn(rc, rc);

        for (uint32_t iStage = 0; iStage < RT_ELEMENTS(pContext->state.aTextureStates); ++iStage)
        {
            for (uint32_t j = 0; j < RT_ELEMENTS(pContext->state.aTextureStates[0]); ++j)
            {
                SVGA3dTextureState *pTextureState = &pContext->state.aTextureStates[iStage][j];

                pHlp->pfnSSMPutU32(pSSM, pTextureState->stage);
                pHlp->pfnSSMPutU32(pSSM, pTextureState->name);
                rc = pHlp->pfnSSMPutU32(pSSM, pTextureState->value);
                AssertRCReturn(rc, rc);
            }
        }

        /* Occlusion query. */
        if (!VMSVGA3DQUERY_EXISTS(&pContext->occlusion))
        {
            pContext->occlusion.enmQueryState = VMSVGA3DQUERYSTATE_NULL;
        }

        /* Save the current query state, because code below can change it. */
        VMSVGA3DQUERYSTATE const enmQueryState = pContext->occlusion.enmQueryState;
        switch (enmQueryState)
        {
            case VMSVGA3DQUERYSTATE_BUILDING:
                /* Stop collecting data. Fetch partial result. Save result. */
                vmsvga3dQueryEnd(pThisCC, cid, SVGA3D_QUERYTYPE_OCCLUSION);
                RT_FALL_THRU();
            case VMSVGA3DQUERYSTATE_ISSUED:
                /* Fetch result. Save result. */
                pContext->occlusion.u32QueryResult = 0;
                vmsvga3dQueryWait(pThisCC, cid, SVGA3D_QUERYTYPE_OCCLUSION, NULL, NULL);
                RT_FALL_THRU();
            case VMSVGA3DQUERYSTATE_SIGNALED:
                /* Save result. Nothing to do here. */
                break;

            default:
                AssertFailed();
                RT_FALL_THRU();
            case VMSVGA3DQUERYSTATE_NULL:
                pContext->occlusion.enmQueryState = VMSVGA3DQUERYSTATE_NULL;
                pContext->occlusion.u32QueryResult = 0;
                break;
        }

        /* Restore the current actual state. */
        pContext->occlusion.enmQueryState = enmQueryState;

        rc = pHlp->pfnSSMPutStructEx(pSSM, &pContext->occlusion, sizeof(pContext->occlusion), 0, g_aVMSVGA3DQUERYFields, NULL);
        AssertRCReturn(rc, rc);
    }

    return VINF_SUCCESS;
}

int vmsvga3dSaveExec(PPDMDEVINS pDevIns, PVGASTATECC pThisCC, PSSMHANDLE pSSM)
{
    PVMSVGA3DSTATE  pState = pThisCC->svga.p3dState;
    AssertReturn(pState, VERR_NO_MEMORY);
    PCPDMDEVHLPR3   pHlp   = pDevIns->pHlpR3;
    int             rc;

    /* Save a copy of the generic 3d state first. */
    rc = pHlp->pfnSSMPutStructEx(pSSM, pState, sizeof(*pState), 0, g_aVMSVGA3DSTATEFields, NULL);
    AssertRCReturn(rc, rc);

#ifdef VMSVGA3D_OPENGL
    /* Save the shared context. */
    if (pState->SharedCtx.id == VMSVGA3D_SHARED_CTX_ID)
    {
        rc = vmsvga3dSaveContext(pHlp, pThisCC, pSSM, &pState->SharedCtx);
        AssertRCReturn(rc, rc);
    }
#endif

    /* Save all active contexts. */
    for (uint32_t i = 0; i < pState->cContexts; i++)
    {
        rc = vmsvga3dSaveContext(pHlp, pThisCC, pSSM, pState->papContexts[i]);
        AssertRCReturn(rc, rc);
    }

    /* Save all active surfaces. */
    for (uint32_t sid = 0; sid < pState->cSurfaces; sid++)
    {
        PVMSVGA3DSURFACE pSurface = pState->papSurfaces[sid];

        /* Save the id first. */
        rc = pHlp->pfnSSMPutU32(pSSM, pSurface->id);
        AssertRCReturn(rc, rc);

        if (pSurface->id != SVGA3D_INVALID_ID)
        {
            /* Save a copy of the surface structure first. */
            rc = pHlp->pfnSSMPutStructEx(pSSM, pSurface, sizeof(*pSurface), 0, g_aVMSVGA3DSURFACEFields, NULL);
            AssertRCReturn(rc, rc);

            /* Save the mip map level info. */
            for (uint32_t face=0; face < pSurface->cFaces; face++)
            {
                for (uint32_t i = 0; i < pSurface->cLevels; i++)
                {
                    uint32_t idx = i + face * pSurface->cLevels;
                    PVMSVGA3DMIPMAPLEVEL pMipmapLevel = &pSurface->paMipmapLevels[idx];

                    /* Save a copy of the mip map level struct. */
                    rc = pHlp->pfnSSMPutStructEx(pSSM, pMipmapLevel, sizeof(*pMipmapLevel), 0, g_aVMSVGA3DMIPMAPLEVELFields, NULL);
                    AssertRCReturn(rc, rc);
                }
            }

            /* Save the mip map level data. */
            for (uint32_t face=0; face < pSurface->cFaces; face++)
            {
                for (uint32_t i = 0; i < pSurface->cLevels; i++)
                {
                    uint32_t idx = i + face * pSurface->cLevels;
                    PVMSVGA3DMIPMAPLEVEL pMipmapLevel = &pSurface->paMipmapLevels[idx];

                    Log(("Surface sid=%u: save mipmap level %d with %x bytes data.\n", sid, i, pMipmapLevel->cbSurface));

                    if (!VMSVGA3DSURFACE_HAS_HW_SURFACE(pSurface))
                    {
                        if (pMipmapLevel->fDirty)
                        {
                            /* Data follows */
                            rc = pHlp->pfnSSMPutBool(pSSM, true);
                            AssertRCReturn(rc, rc);

                            Assert(pMipmapLevel->cbSurface);
                            rc = pHlp->pfnSSMPutMem(pSSM, pMipmapLevel->pSurfaceData, pMipmapLevel->cbSurface);
                            AssertRCReturn(rc, rc);
                        }
                        else
                        {
                            /* No data follows */
                            rc = pHlp->pfnSSMPutBool(pSSM, false);
                            AssertRCReturn(rc, rc);
                        }
                    }
                    else if (vmsvga3dIsLegacyBackend(pThisCC))
                    {
#ifdef VMSVGA3D_DIRECT3D
                        void            *pData;
                        bool             fRenderTargetTexture = false;
                        bool             fTexture = false;
                        bool             fSkipSave = false;
                        HRESULT          hr;

                        Assert(pMipmapLevel->cbSurface);
                        pData = RTMemAllocZ(pMipmapLevel->cbSurface);
                        AssertReturn(pData, VERR_NO_MEMORY);

                        switch (pSurface->enmD3DResType)
                        {
                        case VMSVGA3D_D3DRESTYPE_CUBE_TEXTURE:
                        case VMSVGA3D_D3DRESTYPE_VOLUME_TEXTURE:
                            AssertFailed(); /// @todo
                            fSkipSave = true;
                            break;
                        case VMSVGA3D_D3DRESTYPE_SURFACE:
                        case VMSVGA3D_D3DRESTYPE_TEXTURE:
                        {
                            if (pSurface->f.surfaceFlags & SVGA3D_SURFACE_HINT_DEPTHSTENCIL)
                            {
                               /** @todo unable to easily fetch depth surface data in d3d 9 */
                               fSkipSave = true;
                               break;
                            }

                            fTexture = (pSurface->enmD3DResType == VMSVGA3D_D3DRESTYPE_TEXTURE);
                            fRenderTargetTexture = fTexture && (pSurface->f.surfaceFlags & SVGA3D_SURFACE_HINT_RENDERTARGET);

                            D3DLOCKED_RECT LockedRect;

                            if (fTexture)
                            {
                                if (pSurface->bounce.pTexture)
                                {
                                    if (    !pSurface->fDirty
                                        &&  fRenderTargetTexture
                                        &&  i == 0 /* only the first time */)
                                    {
                                        IDirect3DSurface9 *pSrc, *pDest;

                                        /** @todo stricter checks for associated context */
                                        uint32_t cid = pSurface->idAssociatedContext;

                                        PVMSVGA3DCONTEXT pContext;
                                        rc = vmsvga3dContextFromCid(pState, cid, &pContext);
                                        AssertRCReturn(rc, rc);

                                        hr = pSurface->bounce.pTexture->GetSurfaceLevel(i, &pDest);
                                        AssertMsgReturn(hr == D3D_OK, ("vmsvga3dSaveExec: GetSurfaceLevel failed with %x\n", hr), VERR_INTERNAL_ERROR);

                                        hr = pSurface->u.pTexture->GetSurfaceLevel(i, &pSrc);
                                        AssertMsgReturn(hr == D3D_OK, ("vmsvga3dSaveExec: GetSurfaceLevel failed with %x\n", hr), VERR_INTERNAL_ERROR);

                                        hr = pContext->pDevice->GetRenderTargetData(pSrc, pDest);
                                        AssertMsgReturn(hr == D3D_OK, ("vmsvga3dSaveExec: GetRenderTargetData failed with %x\n", hr), VERR_INTERNAL_ERROR);

                                        pSrc->Release();
                                        pDest->Release();
                                    }

                                    hr = pSurface->bounce.pTexture->LockRect(i, /* texture level */
                                                                             &LockedRect,
                                                                             NULL,
                                                                             D3DLOCK_READONLY);
                                }
                                else
                                    hr = pSurface->u.pTexture->LockRect(i, /* texture level */
                                                                        &LockedRect,
                                                                        NULL,
                                                                        D3DLOCK_READONLY);
                            }
                            else
                                hr = pSurface->u.pSurface->LockRect(&LockedRect,
                                                                    NULL,
                                                                    D3DLOCK_READONLY);
                            AssertMsgReturn(hr == D3D_OK, ("vmsvga3dSaveExec: LockRect failed with %x\n", hr), VERR_INTERNAL_ERROR);

                            /* Copy the data one line at a time in case the internal pitch is different. */
                            for (uint32_t j = 0; j < pMipmapLevel->cBlocksY; ++j)
                            {
                                uint8_t *pu8Dst = (uint8_t *)pData + j * pMipmapLevel->cbSurfacePitch;
                                const uint8_t *pu8Src = (uint8_t *)LockedRect.pBits + j * LockedRect.Pitch;
                                memcpy(pu8Dst, pu8Src, pMipmapLevel->cbSurfacePitch);
                            }

                            if (fTexture)
                            {
                                if (pSurface->bounce.pTexture)
                                {
                                    hr = pSurface->bounce.pTexture->UnlockRect(i);
                                    AssertMsgReturn(hr == D3D_OK, ("vmsvga3dSaveExec: UnlockRect failed with %x\n", hr), VERR_INTERNAL_ERROR);
                                }
                                else
                                    hr = pSurface->u.pTexture->UnlockRect(i);
                            }
                            else
                                hr = pSurface->u.pSurface->UnlockRect();
                            AssertMsgReturn(hr == D3D_OK, ("vmsvga3dSaveExec: UnlockRect failed with %x\n", hr), VERR_INTERNAL_ERROR);
                            break;
                        }

                        case VMSVGA3D_D3DRESTYPE_VERTEX_BUFFER:
                        case VMSVGA3D_D3DRESTYPE_INDEX_BUFFER:
                        {
                            /* Current type of the buffer. */
                            const bool fVertex = (pSurface->enmD3DResType == VMSVGA3D_D3DRESTYPE_VERTEX_BUFFER);

                            uint8_t *pD3DData;

                            if (fVertex)
                                hr = pSurface->u.pVertexBuffer->Lock(0, 0, (void **)&pD3DData, D3DLOCK_READONLY);
                            else
                                hr = pSurface->u.pIndexBuffer->Lock(0, 0, (void **)&pD3DData, D3DLOCK_READONLY);
                            AssertMsg(hr == D3D_OK, ("vmsvga3dSaveExec: Lock %s failed with %x\n", (fVertex) ? "vertex" : "index", hr));

                            memcpy(pData, pD3DData, pMipmapLevel->cbSurface);

                            if (fVertex)
                                hr = pSurface->u.pVertexBuffer->Unlock();
                            else
                                hr = pSurface->u.pIndexBuffer->Unlock();
                            AssertMsg(hr == D3D_OK, ("vmsvga3dSaveExec: Unlock %s failed with %x\n", (fVertex) ? "vertex" : "index", hr));
                            break;
                        }

                        default:
                            AssertFailed();
                            break;
                        }

                        if (!fSkipSave)
                        {
                            /* Data follows */
                            rc = pHlp->pfnSSMPutBool(pSSM, true);
                            AssertRCReturn(rc, rc);

                            /* And write the surface data. */
                            rc = pHlp->pfnSSMPutMem(pSSM, pData, pMipmapLevel->cbSurface);
                            AssertRCReturn(rc, rc);
                        }
                        else
                        {
                            /* No data follows */
                            rc = pHlp->pfnSSMPutBool(pSSM, false);
                            AssertRCReturn(rc, rc);
                        }

                        RTMemFree(pData);

#elif defined(VMSVGA3D_OPENGL)
                        void *pData = NULL;

                        PVMSVGA3DCONTEXT pContext = &pState->SharedCtx;
                        VMSVGA3D_SET_CURRENT_CONTEXT(pState, pContext);

                        Assert(pMipmapLevel->cbSurface);

                        switch (pSurface->enmOGLResType)
                        {
                        default:
                            AssertFailed();
                            RT_FALL_THRU();
                        case VMSVGA3D_OGLRESTYPE_RENDERBUFFER:
                            /** @todo fetch data from the renderbuffer. Not used currently. */
                            /* No data follows */
                            rc = pHlp->pfnSSMPutBool(pSSM, false);
                            AssertRCReturn(rc, rc);
                            break;

                        case VMSVGA3D_OGLRESTYPE_TEXTURE:
                        {
                            GLint activeTexture;

                            pData = RTMemAllocZ(pMipmapLevel->cbSurface);
                            AssertReturn(pData, VERR_NO_MEMORY);

                            glGetIntegerv(GL_TEXTURE_BINDING_2D, &activeTexture);
                            VMSVGA3D_CHECK_LAST_ERROR_WARN(pState, pContext);

                            glBindTexture(GL_TEXTURE_2D, pSurface->oglId.texture);
                            VMSVGA3D_CHECK_LAST_ERROR_WARN(pState, pContext);

                            /* Set row length and alignment of the output data. */
                            VMSVGAPACKPARAMS SavedParams;
                            vmsvga3dOglSetPackParams(pState, pContext, pSurface, &SavedParams);

                            glGetTexImage(GL_TEXTURE_2D,
                                          i,
                                          pSurface->formatGL,
                                          pSurface->typeGL,
                                          pData);
                            VMSVGA3D_CHECK_LAST_ERROR_WARN(pState, pContext);

                            vmsvga3dOglRestorePackParams(pState, pContext, pSurface, &SavedParams);

                            /* Data follows */
                            rc = pHlp->pfnSSMPutBool(pSSM, true);
                            AssertRCReturn(rc, rc);

                            /* And write the surface data. */
                            rc = pHlp->pfnSSMPutMem(pSSM, pData, pMipmapLevel->cbSurface);
                            AssertRCReturn(rc, rc);

                            /* Restore the old active texture. */
                            glBindTexture(GL_TEXTURE_2D, activeTexture);
                            VMSVGA3D_CHECK_LAST_ERROR_WARN(pState, pContext);
                            break;
                        }

                        case VMSVGA3D_OGLRESTYPE_BUFFER:
                        {
                            uint8_t *pBufferData;

                            pState->ext.glBindBuffer(GL_ARRAY_BUFFER, pSurface->oglId.buffer);
                            VMSVGA3D_CHECK_LAST_ERROR(pState, pContext);

                            pBufferData = (uint8_t *)pState->ext.glMapBuffer(GL_ARRAY_BUFFER, GL_READ_ONLY);
                            VMSVGA3D_CHECK_LAST_ERROR(pState, pContext);
                            Assert(pBufferData);

                            /* Data follows */
                            rc = pHlp->pfnSSMPutBool(pSSM, true);
                            AssertRCReturn(rc, rc);

                            /* And write the surface data. */
                            rc = pHlp->pfnSSMPutMem(pSSM, pBufferData, pMipmapLevel->cbSurface);
                            AssertRCReturn(rc, rc);

                            pState->ext.glUnmapBuffer(GL_ARRAY_BUFFER);
                            VMSVGA3D_CHECK_LAST_ERROR(pState, pContext);

                            pState->ext.glBindBuffer(GL_ARRAY_BUFFER, 0);
                            VMSVGA3D_CHECK_LAST_ERROR(pState, pContext);

                        }
                        }
                        if (pData)
                            RTMemFree(pData);
#else
#error "Unexpected 3d backend"
#endif
                    }
                    else
                    {
                        /** @todo DX backend. */
                        Assert(!vmsvga3dIsLegacyBackend(pThisCC));

                        /* No data follows */
                        rc = pHlp->pfnSSMPutBool(pSSM, false);
                        AssertRCReturn(rc, rc);
                    }
                }
            }
        }
    }
    return VINF_SUCCESS;
}

int vmsvga3dSaveShaderConst(PVMSVGA3DCONTEXT pContext, uint32_t reg, SVGA3dShaderType type, SVGA3dShaderConstType ctype,
                            uint32_t val1, uint32_t val2, uint32_t val3, uint32_t val4)
{
    /* Choose a sane upper limit. */
    AssertReturn(reg < _32K, VERR_INVALID_PARAMETER);

    if (type == SVGA3D_SHADERTYPE_VS)
    {
        if (pContext->state.cVertexShaderConst <= reg)
        {
            pContext->state.paVertexShaderConst = (PVMSVGASHADERCONST)RTMemRealloc(pContext->state.paVertexShaderConst, sizeof(VMSVGASHADERCONST) * (reg + 1));
            AssertReturn(pContext->state.paVertexShaderConst, VERR_NO_MEMORY);
            for (uint32_t i = pContext->state.cVertexShaderConst; i < reg + 1; i++)
                pContext->state.paVertexShaderConst[i].fValid = false;
            pContext->state.cVertexShaderConst = reg + 1;
        }

        pContext->state.paVertexShaderConst[reg].fValid   = true;
        pContext->state.paVertexShaderConst[reg].ctype    = ctype;
        pContext->state.paVertexShaderConst[reg].value[0] = val1;
        pContext->state.paVertexShaderConst[reg].value[1] = val2;
        pContext->state.paVertexShaderConst[reg].value[2] = val3;
        pContext->state.paVertexShaderConst[reg].value[3] = val4;
    }
    else
    {
        Assert(type == SVGA3D_SHADERTYPE_PS);
        if (pContext->state.cPixelShaderConst <= reg)
        {
            pContext->state.paPixelShaderConst = (PVMSVGASHADERCONST)RTMemRealloc(pContext->state.paPixelShaderConst, sizeof(VMSVGASHADERCONST) * (reg + 1));
            AssertReturn(pContext->state.paPixelShaderConst, VERR_NO_MEMORY);
            for (uint32_t i = pContext->state.cPixelShaderConst; i < reg + 1; i++)
                pContext->state.paPixelShaderConst[i].fValid = false;
            pContext->state.cPixelShaderConst = reg + 1;
        }

        pContext->state.paPixelShaderConst[reg].fValid   = true;
        pContext->state.paPixelShaderConst[reg].ctype    = ctype;
        pContext->state.paPixelShaderConst[reg].value[0] = val1;
        pContext->state.paPixelShaderConst[reg].value[1] = val2;
        pContext->state.paPixelShaderConst[reg].value[2] = val3;
        pContext->state.paPixelShaderConst[reg].value[3] = val4;
    }

    return VINF_SUCCESS;
}

