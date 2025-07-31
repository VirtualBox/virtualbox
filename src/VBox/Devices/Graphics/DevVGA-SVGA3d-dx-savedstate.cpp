/* $Id: DevVGA-SVGA3d-dx-savedstate.cpp 110140 2025-07-07 18:00:16Z vitali.pelenjow@oracle.com $ */
/** @file
 * DevSVGA3d - VMWare SVGA device, 3D parts - DX backend saved state.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_VMSVGA
#include <VBox/AssertGuest.h>
#include <iprt/errcore.h>
#include <VBox/log.h>
#include <VBox/vmm/pdmdev.h>

#include <iprt/assert.h>
#include <iprt/mem.h>

#include <VBoxVideo.h> /* required by DevVGA.h */

/* should go BEFORE any other DevVGA include to make all DevVGA.h config defines be visible */
#include "DevVGA.h"

#include "DevVGA-SVGA.h"
#include "DevVGA-SVGA3d.h"
#include "DevVGA-SVGA3d-internal.h"
#include "DevVGA-SVGA-internal.h"

/*
 * Load
 */

static int vmsvga3dDXLoadSurface(PCPDMDEVHLPR3 pHlp, PVGASTATECC pThisCC, PSSMHANDLE pSSM)
{
    PVMSVGA3DSTATE p3dState = pThisCC->svga.p3dState;
    int rc;

    uint32_t sid;
    rc = pHlp->pfnSSMGetU32(pSSM, &sid);
    AssertRCReturn(rc, rc);

    if (sid == SVGA3D_INVALID_ID)
        return VINF_SUCCESS;

    /* Define the surface. */
    SVGAOTableSurfaceEntry entrySurface;
    rc = vmsvgaR3OTableReadSurface(pThisCC->svga.pSvgaR3State, sid, &entrySurface);
    AssertRCReturn(rc, rc);

    /** @todo fAllocMipLevels=false and alloc miplevels if there is data to be loaded. */
    rc = vmsvga3dSurfaceDefine(pThisCC, sid, RT_MAKE_U64(entrySurface.surface1Flags, entrySurface.surface2Flags), entrySurface.format,
                               entrySurface.multisampleCount, (SVGA3dMSPattern)entrySurface.multisamplePattern, (SVGA3dMSQualityLevel)entrySurface.qualityLevel, entrySurface.autogenFilter,
                               entrySurface.numMipLevels, &entrySurface.size,
                               entrySurface.arraySize, entrySurface.bufferByteStride,
                               /* fAllocMipLevels = */ true);
    AssertRCReturn(rc, rc);

    PVMSVGA3DSURFACE pSurface = p3dState->papSurfaces[sid];
    AssertReturn(pSurface->id == sid, VERR_INTERNAL_ERROR);

    /* Load the surface fields which are not part of SVGAOTableSurfaceEntry. */
    pHlp->pfnSSMGetU32(pSSM, &pSurface->idAssociatedContext);

    /* Load miplevels data to the surface buffers. */
    for (uint32_t j = 0; j < pSurface->cLevels * pSurface->surfaceDesc.numArrayElements; j++)
    {
        PVMSVGA3DMIPMAPLEVEL pMipmapLevel = &pSurface->paMipmapLevels[j];

        /* vmsvga3dSurfaceDefine already allocated the surface data buffer. */
        Assert(pMipmapLevel->cbSurface);
        AssertReturn(pMipmapLevel->pSurfaceData, VERR_INTERNAL_ERROR);

        /* Fetch the data present boolean first. */
        bool fDataPresent;
        rc = pHlp->pfnSSMGetBool(pSSM, &fDataPresent);
        AssertRCReturn(rc, rc);

        if (fDataPresent)
        {
            rc = pHlp->pfnSSMGetMem(pSSM, pMipmapLevel->pSurfaceData, pMipmapLevel->cbSurface);
            AssertRCReturn(rc, rc);

            pMipmapLevel->fDirty = true;
            pSurface->fDirty     = true;
        }
        else
            pMipmapLevel->fDirty = false;
    }

    return VINF_SUCCESS;
}

static int vmsvga3dDXLoadContext(PCPDMDEVHLPR3 pHlp, PVGASTATECC pThisCC, PSSMHANDLE pSSM)
{
    PVMSVGAR3STATE pSvgaR3State = pThisCC->svga.pSvgaR3State;
    PVMSVGA3DSTATE p3dState = pThisCC->svga.p3dState;
    uint32_t u32;
    int rc;

    uint32_t cid;
    rc = pHlp->pfnSSMGetU32(pSSM, &cid);
    AssertRCReturn(rc, rc);

    if (cid == SVGA3D_INVALID_ID)
        return VINF_SUCCESS;

    /* Define the context. */
    rc = vmsvga3dDXDefineContext(pThisCC, cid);
    AssertRCReturn(rc, rc);

    PVMSVGA3DDXCONTEXT pDXContext = p3dState->papDXContexts[cid];
    AssertReturn(pDXContext->cid == cid, VERR_INTERNAL_ERROR);

    /* Load the context. */
    rc = pHlp->pfnSSMGetU32(pSSM, &u32);
    AssertRCReturn(rc, rc);
    AssertReturn(u32 == sizeof(SVGADXContextMobFormat), VERR_INVALID_STATE);

    pHlp->pfnSSMGetMem(pSSM, &pDXContext->svgaDXContext, sizeof(SVGADXContextMobFormat));

    uint32_t cCOTMobs;
    rc = pHlp->pfnSSMGetU32(pSSM, &cCOTMobs);
    AssertLogRelRCReturn(rc, rc);
    AssertReturn(cCOTMobs <= RT_ELEMENTS(pDXContext->aCOTMobs), VERR_INVALID_STATE);

    for (unsigned i = 0; i < cCOTMobs; ++i)
    {
        rc = pHlp->pfnSSMGetU32(pSSM, &u32);
        AssertLogRelRCReturn(rc, rc);
        pDXContext->aCOTMobs[i] = vmsvgaR3MobGet(pSvgaR3State, u32);
        Assert(pDXContext->aCOTMobs[i] || u32 == SVGA_ID_INVALID);
    }

    struct
    {
        SVGACOTableType COTableType;
        uint32_t cbEntry;
        uint32_t *pcEntries;
        void **ppaEntries;
    } cot[] =
    {
        {SVGA_COTABLE_RTVIEW,          sizeof(SVGACOTableDXRTViewEntry),          &pDXContext->cot.cRTView,          (void **)&pDXContext->cot.paRTView},
        {SVGA_COTABLE_DSVIEW,          sizeof(SVGACOTableDXDSViewEntry),          &pDXContext->cot.cDSView,          (void **)&pDXContext->cot.paDSView},
        {SVGA_COTABLE_SRVIEW,          sizeof(SVGACOTableDXSRViewEntry),          &pDXContext->cot.cSRView,          (void **)&pDXContext->cot.paSRView},
        {SVGA_COTABLE_ELEMENTLAYOUT,   sizeof(SVGACOTableDXElementLayoutEntry),   &pDXContext->cot.cElementLayout,   (void **)&pDXContext->cot.paElementLayout},
        {SVGA_COTABLE_BLENDSTATE,      sizeof(SVGACOTableDXBlendStateEntry),      &pDXContext->cot.cBlendState,      (void **)&pDXContext->cot.paBlendState},
        {SVGA_COTABLE_DEPTHSTENCIL,    sizeof(SVGACOTableDXDepthStencilEntry),    &pDXContext->cot.cDepthStencil,    (void **)&pDXContext->cot.paDepthStencil},
        {SVGA_COTABLE_RASTERIZERSTATE, sizeof(SVGACOTableDXRasterizerStateEntry), &pDXContext->cot.cRasterizerState, (void **)&pDXContext->cot.paRasterizerState},
        {SVGA_COTABLE_SAMPLER,         sizeof(SVGACOTableDXSamplerEntry),         &pDXContext->cot.cSampler,         (void **)&pDXContext->cot.paSampler},
        {SVGA_COTABLE_STREAMOUTPUT,    sizeof(SVGACOTableDXStreamOutputEntry),    &pDXContext->cot.cStreamOutput,    (void **)&pDXContext->cot.paStreamOutput},
        {SVGA_COTABLE_DXQUERY,         sizeof(SVGACOTableDXQueryEntry),           &pDXContext->cot.cQuery,           (void **)&pDXContext->cot.paQuery},
        {SVGA_COTABLE_DXSHADER,        sizeof(SVGACOTableDXShaderEntry),          &pDXContext->cot.cShader,          (void **)&pDXContext->cot.paShader},
        {SVGA_COTABLE_UAVIEW,          sizeof(SVGACOTableDXUAViewEntry),          &pDXContext->cot.cUAView,          (void **)&pDXContext->cot.paUAView},
        {VBSVGA_COTABLE_VIDEOPROCESSOR, sizeof(VBSVGACOTableDXVideoProcessorEntry), &pDXContext->cot.cVideoProcessor, (void **)&pDXContext->cot.paVideoProcessor},
        {VBSVGA_COTABLE_VDOV,          sizeof(VBSVGACOTableDXVideoDecoderOutputViewEntry), &pDXContext->cot.cVideoDecoderOutputView, (void **)&pDXContext->cot.paVideoDecoderOutputView},
        {VBSVGA_COTABLE_VIDEODECODER,  sizeof(VBSVGACOTableDXVideoDecoderEntry), &pDXContext->cot.cVideoDecoder, (void **)&pDXContext->cot.paVideoDecoder},
        {VBSVGA_COTABLE_VPIV,          sizeof(VBSVGACOTableDXVideoProcessorInputViewEntry), &pDXContext->cot.cVideoProcessorInputView, (void **)&pDXContext->cot.paVideoProcessorInputView},
        {VBSVGA_COTABLE_VPOV,          sizeof(VBSVGACOTableDXVideoProcessorOutputViewEntry), &pDXContext->cot.cVideoProcessorOutputView, (void **)&pDXContext->cot.paVideoProcessorOutputView},
    };

    AssertCompile(RT_ELEMENTS(cot) == RT_ELEMENTS(pDXContext->aCOTMobs));
    for (unsigned i = 0; i < cCOTMobs; ++i)
    {
        uint32_t idxCOTable;
        if (cot[i].COTableType < SVGA_COTABLE_MAX)
            idxCOTable = cot[i].COTableType;
        else if (cot[i].COTableType >= VBSVGA_COTABLE_MIN && cot[i].COTableType < VBSVGA_COTABLE_MAX)
            idxCOTable = SVGA_COTABLE_MAX + (cot[i].COTableType - VBSVGA_COTABLE_MIN);
        else
            AssertFailedReturn(VERR_INVALID_STATE);

        uint32_t cEntries;
        pHlp->pfnSSMGetU32(pSSM, &cEntries);
        rc = pHlp->pfnSSMGetU32(pSSM, &u32);
        AssertRCReturn(rc, rc);
        AssertReturn(u32 == cot[i].cbEntry, VERR_INVALID_STATE);

        *cot[i].pcEntries = cEntries;
        *cot[i].ppaEntries = vmsvgaR3MobBackingStorePtr(pDXContext->aCOTMobs[idxCOTable], 0);

        if (cEntries)
        {
            rc = pSvgaR3State->pFuncsDX->pfnDXSetCOTable(pThisCC, pDXContext, cot[i].COTableType, cEntries);
            AssertLogRelRCReturn(rc, rc);
        }
    }

    rc = pSvgaR3State->pFuncsDX->pfnDXLoadState(pThisCC, pDXContext, pHlp, pSSM);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

int vmsvga3dDXLoadExec(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    RT_NOREF(pThis, uPass);

    if (uVersion < VGA_SAVEDSTATE_VERSION_VMSVGA_DX)
        AssertFailedReturn(VERR_INVALID_STATE);

    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;
    PVMSVGAR3STATE pSvgaR3State = pThisCC->svga.pSvgaR3State;
    PVMSVGA3DSTATE p3dState = pThisCC->svga.p3dState;
    int rc;

    /*
     * VMSVGA3DSTATE
     */
    pHlp->pfnSSMGetU32(pSSM, &p3dState->cSurfaces);
    rc = pHlp->pfnSSMGetU32(pSSM, &p3dState->cDXContexts);
    AssertRCReturn(rc, rc);

    /*
     * Surfaces
     */
    if (p3dState->cSurfaces)
    {
        p3dState->papSurfaces = (PVMSVGA3DSURFACE *)RTMemAlloc(p3dState->cSurfaces * sizeof(PVMSVGA3DSURFACE));
        AssertReturn(p3dState->papSurfaces, VERR_NO_MEMORY);
        for (uint32_t i = 0; i < p3dState->cSurfaces; ++i)
        {
            p3dState->papSurfaces[i] = (PVMSVGA3DSURFACE)RTMemAllocZ(sizeof(VMSVGA3DSURFACE));
            AssertPtrReturn(p3dState->papSurfaces[i], VERR_NO_MEMORY);
            p3dState->papSurfaces[i]->id = SVGA3D_INVALID_ID;
        }

        for (uint32_t i = 0; i < p3dState->cSurfaces; ++i)
        {
            rc = vmsvga3dDXLoadSurface(pHlp, pThisCC, pSSM);
            AssertRCReturn(rc, rc);
        }
    }
    else
        p3dState->papSurfaces = NULL;

    /*
     * DX contexts
     */
    if (p3dState->cDXContexts)
    {
        p3dState->papDXContexts = (PVMSVGA3DDXCONTEXT *)RTMemAlloc(p3dState->cDXContexts * sizeof(PVMSVGA3DDXCONTEXT));
        AssertReturn(p3dState->papDXContexts, VERR_NO_MEMORY);
        for (uint32_t i = 0; i < p3dState->cDXContexts; ++i)
        {
            p3dState->papDXContexts[i] = (PVMSVGA3DDXCONTEXT)RTMemAllocZ(sizeof(VMSVGA3DDXCONTEXT));
            AssertPtrReturn(p3dState->papDXContexts[i], VERR_NO_MEMORY);
            p3dState->papDXContexts[i]->cid = SVGA3D_INVALID_ID;
        }

        for (uint32_t i = 0; i < p3dState->cDXContexts; ++i)
        {
            rc = vmsvga3dDXLoadContext(pHlp, pThisCC, pSSM);
            AssertRCReturn(rc, rc);
        }
    }
    else
        p3dState->papDXContexts = NULL;

    if (pSvgaR3State->idDXContextCurrent != SVGA_ID_INVALID)
        vmsvga3dDXSwitchContext(pThisCC, pSvgaR3State->idDXContextCurrent);

    return VINF_SUCCESS;
}

/*
 * Save
 */

static int vmsvga3dDXSaveSurface(PCPDMDEVHLPR3 pHlp, PVGASTATECC pThisCC, PSSMHANDLE pSSM, PVMSVGA3DSURFACE pSurface)
{
    RT_NOREF(pThisCC);
    int rc;

    rc = pHlp->pfnSSMPutU32(pSSM, pSurface->id);
    AssertRCReturn(rc, rc);

    if (pSurface->id == SVGA3D_INVALID_ID)
        return VINF_SUCCESS;

    /* Save the surface fields which are not part of SVGAOTableSurfaceEntry. */
    pHlp->pfnSSMPutU32(pSSM, pSurface->idAssociatedContext);

    for (uint32_t iArray = 0; iArray < pSurface->surfaceDesc.numArrayElements; ++iArray)
    {
        for (uint32_t iMipmap = 0; iMipmap < pSurface->cLevels; ++iMipmap)
        {
            uint32_t idx = iMipmap + iArray * pSurface->cLevels;
            PVMSVGA3DMIPMAPLEVEL pMipmapLevel = &pSurface->paMipmapLevels[idx];

            /* Multisample surface content can't be accessed. */
            if (pSurface->surfaceDesc.multisampleCount > 1)
            {
                /* No data follows */
                rc = pHlp->pfnSSMPutBool(pSSM, false);
                AssertRCReturn(rc, rc);
                continue;
            }

            if (!VMSVGA3DSURFACE_HAS_HW_SURFACE(pSurface))
            {
                if (pMipmapLevel->pSurfaceData)
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
            else
            {
                SVGA3dSurfaceImageId image;
                image.sid = pSurface->id;
                image.face = iArray;
                image.mipmap = iMipmap;

                VMSGA3D_BOX_DIMENSIONS dims;
                rc = vmsvga3dGetBoxDimensions(pThisCC, &image, NULL, &dims);
                AssertRCReturn(rc, rc);

                VMSVGA3D_MAPPED_SURFACE map;
                rc = vmsvga3dSurfaceMap(pThisCC, &image, NULL, VMSVGA3D_SURFACE_MAP_READ, VMSVGA3D_MAP_F_NONE, &map);
                if (RT_SUCCESS(rc))
                {
                    /* Save mapped surface data. */
                    pHlp->pfnSSMPutBool(pSSM, true);
                    if (map.cbRow == map.cbRowPitch)
                    {
                        rc = pHlp->pfnSSMPutMem(pSSM, map.pvData, pMipmapLevel->cbSurface);
                        AssertRCReturn(rc, rc);
                    }
                    else
                    {
                        uint8_t *pu8Map = (uint8_t *)map.pvData;
                        for (uint32_t z = 0; z < map.box.d; ++z)
                        {
                            uint8_t *pu8MapPlane = pu8Map;
                            for (uint32_t y = 0; y < map.cRows; ++y)
                            {
                                rc = pHlp->pfnSSMPutMem(pSSM, pu8MapPlane, dims.cbRow);
                                AssertRCReturn(rc, rc);

                                pu8MapPlane += map.cbRowPitch;
                            }
                            pu8Map += map.cbDepthPitch;
                        }
                    }

                    vmsvga3dSurfaceUnmap(pThisCC, &image, &map, false);
                }
                else
                {
                    AssertFailed();

                    /* No data follows */
                    rc = pHlp->pfnSSMPutBool(pSSM, false);
                    AssertRCReturn(rc, rc);
                }
            }
        }
    }

    return VINF_SUCCESS;
}

static int vmsvga3dDXSaveContext(PCPDMDEVHLPR3 pHlp, PVGASTATECC pThisCC, PSSMHANDLE pSSM, PVMSVGA3DDXCONTEXT pDXContext)
{
    PVMSVGAR3STATE pSvgaR3State = pThisCC->svga.pSvgaR3State;
    int rc;

    rc = pHlp->pfnSSMPutU32(pSSM, pDXContext->cid);
    AssertRCReturn(rc, rc);

    if (pDXContext->cid == SVGA3D_INVALID_ID)
        return VINF_SUCCESS;

    /* Save the context. */
    pHlp->pfnSSMPutU32(pSSM, sizeof(SVGADXContextMobFormat));
    pHlp->pfnSSMPutMem(pSSM, &pDXContext->svgaDXContext, sizeof(SVGADXContextMobFormat));

    rc = pHlp->pfnSSMPutU32(pSSM, RT_ELEMENTS(pDXContext->aCOTMobs));
    AssertLogRelRCReturn(rc, rc);
    for (unsigned i = 0; i < RT_ELEMENTS(pDXContext->aCOTMobs); ++i)
    {
        uint32_t const mobId = vmsvgaR3MobId(pDXContext->aCOTMobs[i]);
        rc = pHlp->pfnSSMPutU32(pSSM, mobId);
        AssertLogRelRCReturn(rc, rc);
    }

    struct
    {
        uint32_t cEntries;
        uint32_t cbEntry;
        void *paEntries;
    } cot[] =
    {
        {pDXContext->cot.cRTView,          sizeof(SVGACOTableDXRTViewEntry),          pDXContext->cot.paRTView},
        {pDXContext->cot.cDSView,          sizeof(SVGACOTableDXDSViewEntry),          pDXContext->cot.paDSView},
        {pDXContext->cot.cSRView,          sizeof(SVGACOTableDXSRViewEntry),          pDXContext->cot.paSRView},
        {pDXContext->cot.cElementLayout,   sizeof(SVGACOTableDXElementLayoutEntry),   pDXContext->cot.paElementLayout},
        {pDXContext->cot.cBlendState,      sizeof(SVGACOTableDXBlendStateEntry),      pDXContext->cot.paBlendState},
        {pDXContext->cot.cDepthStencil,    sizeof(SVGACOTableDXDepthStencilEntry),    pDXContext->cot.paDepthStencil},
        {pDXContext->cot.cRasterizerState, sizeof(SVGACOTableDXRasterizerStateEntry), pDXContext->cot.paRasterizerState},
        {pDXContext->cot.cSampler,         sizeof(SVGACOTableDXSamplerEntry),         pDXContext->cot.paSampler},
        {pDXContext->cot.cStreamOutput,    sizeof(SVGACOTableDXStreamOutputEntry),    pDXContext->cot.paStreamOutput},
        {pDXContext->cot.cQuery,           sizeof(SVGACOTableDXQueryEntry),           pDXContext->cot.paQuery},
        {pDXContext->cot.cShader,          sizeof(SVGACOTableDXShaderEntry),          pDXContext->cot.paShader},
        {pDXContext->cot.cUAView,          sizeof(SVGACOTableDXUAViewEntry),          pDXContext->cot.paUAView},
        {pDXContext->cot.cVideoProcessor,  sizeof(VBSVGACOTableDXVideoProcessorEntry), pDXContext->cot.paVideoProcessor},
        {pDXContext->cot.cVideoDecoderOutputView, sizeof(VBSVGACOTableDXVideoDecoderOutputViewEntry), pDXContext->cot.paVideoDecoderOutputView},
        {pDXContext->cot.cVideoDecoder,    sizeof(VBSVGACOTableDXVideoDecoderEntry),  pDXContext->cot.paVideoDecoder},
        {pDXContext->cot.cVideoProcessorInputView, sizeof(VBSVGACOTableDXVideoProcessorInputViewEntry), pDXContext->cot.paVideoProcessorInputView},
        {pDXContext->cot.cVideoProcessorOutputView, sizeof(VBSVGACOTableDXVideoProcessorOutputViewEntry), pDXContext->cot.paVideoProcessorOutputView},
    };

    AssertCompile(RT_ELEMENTS(cot) == RT_ELEMENTS(pDXContext->aCOTMobs));
    for (unsigned i = 0; i < RT_ELEMENTS(cot); ++i)
    {
        pHlp->pfnSSMPutU32(pSSM, cot[i].cEntries);
        rc = pHlp->pfnSSMPutU32(pSSM, cot[i].cbEntry);
        AssertLogRelRCReturn(rc, rc);
    }

    rc = pSvgaR3State->pFuncsDX->pfnDXSaveState(pThisCC, pDXContext, pHlp, pSSM);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

int vmsvga3dDXSaveExec(PPDMDEVINS pDevIns, PVGASTATECC pThisCC, PSSMHANDLE pSSM)
{
    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;
    PVMSVGA3DSTATE p3dState = pThisCC->svga.p3dState;
    int rc;

    /*
     * VMSVGA3DSTATE
     */
    pHlp->pfnSSMPutU32(pSSM, p3dState->cSurfaces);
    rc = pHlp->pfnSSMPutU32(pSSM, p3dState->cDXContexts);
    AssertRCReturn(rc, rc);

    /*
     * Surfaces
     */
    for (uint32_t sid = 0; sid < p3dState->cSurfaces; ++sid)
    {
        rc = vmsvga3dDXSaveSurface(pHlp, pThisCC, pSSM, p3dState->papSurfaces[sid]);
        AssertRCReturn(rc, rc);
    }

    /*
     * DX contexts
     */
    for (uint32_t cid = 0; cid < p3dState->cDXContexts; ++cid)
    {
        rc = vmsvga3dDXSaveContext(pHlp, pThisCC, pSSM, p3dState->papDXContexts[cid]);
        AssertRCReturn(rc, rc);
    }

    return VINF_SUCCESS;
}
