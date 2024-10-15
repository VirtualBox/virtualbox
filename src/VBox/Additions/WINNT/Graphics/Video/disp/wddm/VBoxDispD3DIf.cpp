/* $Id: VBoxDispD3DIf.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#include "VBoxDispD3DIf.h"
#include "VBoxDispD3DCmn.h"

#include <iprt/assert.h>

/** Convert a given FourCC code to a D3DDDIFORMAT enum. */
#define VBOXWDDM_D3DDDIFORMAT_FROM_FOURCC(_a, _b, _c, _d) \
    ((D3DDDIFORMAT)MAKEFOURCC(_a, _b, _c, _d))

static FORMATOP gVBoxFormatOpsBase[] = {
    {D3DDDIFMT_X8R8G8B8, FORMATOP_DISPLAYMODE, 0, 0, 0},

    {D3DDDIFMT_R8G8B8, FORMATOP_DISPLAYMODE, 0, 0, 0},

    {D3DDDIFMT_R5G6B5, FORMATOP_DISPLAYMODE, 0, 0, 0},

    {D3DDDIFMT_P8, FORMATOP_DISPLAYMODE, 0, 0, 0},
};

static DDSURFACEDESC gVBoxSurfDescsBase[] = {
        {
            sizeof (DDSURFACEDESC), /*    DWORD   dwSize;  */
            DDSD_CAPS | DDSD_PIXELFORMAT,    /* DWORD dwFlags;    */
            0,    /* DWORD dwHeight;   */
            0,    /* DWORD dwWidth;    */
            {
                0, /* Union             */
                   /*   LONG lPitch; */
                   /*   DWORD dwLinearSize; */
            },
            0,  /*    DWORD dwBackBufferCount; */
            {
                0, /* Union */
                   /*  DWORD dwMipMapCount; */
                   /*    DWORD dwZBufferBitDepth; */
                   /*   DWORD dwRefreshRate; */
            },
            0, /*    DWORD dwAlphaBitDepth; */
            0, /*   DWORD dwReserved; */
            NULL, /*   LPVOID lpSurface; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY  ddckCKDestOverlay; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY  ddckCKDestBlt; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY  ddckCKSrcOverlay; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY ddckCKSrcBlt; */
            {
                sizeof (DDPIXELFORMAT), /* DWORD dwSize; */
                DDPF_RGB, /* DWORD dwFlags; */
                0, /* DWORD dwFourCC; */
                {
                    32, /* union */
                       /* DWORD dwRGBBitCount; */
                       /* DWORD dwYUVBitCount; */
                       /* DWORD dwZBufferBitDepth; */
                       /* DWORD dwAlphaBitDepth; */
                       /* DWORD dwLuminanceBitCount; */
                       /* DWORD dwBumpBitCount; */
                },
                {
                    0xff0000, /* union */
                       /* DWORD dwRBitMask; */
                       /* DWORD dwYBitMask; */
                        /* DWORD dwStencilBitDepth; */
                        /* DWORD dwLuminanceBitMask; */
                        /* DWORD dwBumpDuBitMask; */
                },
                {
                    0xff00,
                        /* DWORD dwGBitMask; */
                        /* DWORD dwUBitMask; */
                        /* DWORD dwZBitMask; */
                        /* DWORD dwBumpDvBitMask; */
                },
                {
                    0xff,
                        /* DWORD dwBBitMask; */
                        /* DWORD dwVBitMask; */
                        /* DWORD dwStencilBitMask; */
                        /* DWORD dwBumpLuminanceBitMask; */
                },
                {
                    0,
                        /* DWORD dwRGBAlphaBitMask; */
                        /* DWORD dwYUVAlphaBitMask; */
                        /* DWORD dwLuminanceAlphaBitMask; */
                        /* DWORD dwRGBZBitMask; */
                        /* DWORD dwYUVZBitMask; */
                },
            }, /* DDPIXELFORMAT ddpfPixelFormat; */
            {
                DDSCAPS_BACKBUFFER
                | DDSCAPS_COMPLEX
                | DDSCAPS_FLIP
                | DDSCAPS_FRONTBUFFER
                | DDSCAPS_LOCALVIDMEM
                | DDSCAPS_PRIMARYSURFACE
                | DDSCAPS_VIDEOMEMORY
                | DDSCAPS_VISIBLE   /* DWORD dwCaps; */
            } /* DDSCAPS ddsCaps; */
        },
        {
            sizeof (DDSURFACEDESC), /*    DWORD   dwSize;  */
            DDSD_CAPS | DDSD_PIXELFORMAT,    /* DWORD dwFlags;    */
            0,    /* DWORD dwHeight;   */
            0,    /* DWORD dwWidth;    */
            {
                0, /* Union             */
                   /*   LONG lPitch; */
                   /*   DWORD dwLinearSize; */
            },
            0,  /*    DWORD dwBackBufferCount; */
            {
                0, /* Union */
                   /*  DWORD dwMipMapCount; */
                   /*    DWORD dwZBufferBitDepth; */
                   /*   DWORD dwRefreshRate; */
            },
            0, /*    DWORD dwAlphaBitDepth; */
            0, /*   DWORD dwReserved; */
            NULL, /*   LPVOID lpSurface; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY  ddckCKDestOverlay; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY  ddckCKDestBlt; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY  ddckCKSrcOverlay; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY ddckCKSrcBlt; */
            {
                sizeof (DDPIXELFORMAT), /* DWORD dwSize; */
                DDPF_RGB, /* DWORD dwFlags; */
                0, /* DWORD dwFourCC; */
                {
                    24, /* union */
                       /* DWORD dwRGBBitCount; */
                       /* DWORD dwYUVBitCount; */
                       /* DWORD dwZBufferBitDepth; */
                       /* DWORD dwAlphaBitDepth; */
                       /* DWORD dwLuminanceBitCount; */
                       /* DWORD dwBumpBitCount; */
                },
                {
                    0xff0000, /* union */
                       /* DWORD dwRBitMask; */
                       /* DWORD dwYBitMask; */
                        /* DWORD dwStencilBitDepth; */
                        /* DWORD dwLuminanceBitMask; */
                        /* DWORD dwBumpDuBitMask; */
                },
                {
                    0xff00,
                        /* DWORD dwGBitMask; */
                        /* DWORD dwUBitMask; */
                        /* DWORD dwZBitMask; */
                        /* DWORD dwBumpDvBitMask; */
                },
                {
                    0xff,
                        /* DWORD dwBBitMask; */
                        /* DWORD dwVBitMask; */
                        /* DWORD dwStencilBitMask; */
                        /* DWORD dwBumpLuminanceBitMask; */
                },
                {
                    0,
                        /* DWORD dwRGBAlphaBitMask; */
                        /* DWORD dwYUVAlphaBitMask; */
                        /* DWORD dwLuminanceAlphaBitMask; */
                        /* DWORD dwRGBZBitMask; */
                        /* DWORD dwYUVZBitMask; */
                },
            }, /* DDPIXELFORMAT ddpfPixelFormat; */
            {
                DDSCAPS_BACKBUFFER
                | DDSCAPS_COMPLEX
                | DDSCAPS_FLIP
                | DDSCAPS_FRONTBUFFER
                | DDSCAPS_LOCALVIDMEM
                | DDSCAPS_PRIMARYSURFACE
                | DDSCAPS_VIDEOMEMORY
                | DDSCAPS_VISIBLE  /* DWORD dwCaps; */
            } /* DDSCAPS ddsCaps; */
        },
        {
            sizeof (DDSURFACEDESC), /*    DWORD   dwSize;  */
            DDSD_CAPS | DDSD_PIXELFORMAT,    /* DWORD dwFlags;    */
            0,    /* DWORD dwHeight;   */
            0,    /* DWORD dwWidth;    */
            {
                0, /* Union             */
                   /*   LONG lPitch; */
                   /*   DWORD dwLinearSize; */
            },
            0,  /*    DWORD dwBackBufferCount; */
            {
                0, /* Union */
                   /*  DWORD dwMipMapCount; */
                   /*    DWORD dwZBufferBitDepth; */
                   /*   DWORD dwRefreshRate; */
            },
            0, /*    DWORD dwAlphaBitDepth; */
            0, /*   DWORD dwReserved; */
            NULL, /*   LPVOID lpSurface; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY  ddckCKDestOverlay; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY  ddckCKDestBlt; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY  ddckCKSrcOverlay; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY ddckCKSrcBlt; */
            {
                sizeof (DDPIXELFORMAT), /* DWORD dwSize; */
                DDPF_RGB, /* DWORD dwFlags; */
                0, /* DWORD dwFourCC; */
                {
                    16, /* union */
                       /* DWORD dwRGBBitCount; */
                       /* DWORD dwYUVBitCount; */
                       /* DWORD dwZBufferBitDepth; */
                       /* DWORD dwAlphaBitDepth; */
                       /* DWORD dwLuminanceBitCount; */
                       /* DWORD dwBumpBitCount; */
                },
                {
                    0xf800, /* union */
                       /* DWORD dwRBitMask; */
                       /* DWORD dwYBitMask; */
                        /* DWORD dwStencilBitDepth; */
                        /* DWORD dwLuminanceBitMask; */
                        /* DWORD dwBumpDuBitMask; */
                },
                {
                    0x7e0,
                        /* DWORD dwGBitMask; */
                        /* DWORD dwUBitMask; */
                        /* DWORD dwZBitMask; */
                        /* DWORD dwBumpDvBitMask; */
                },
                {
                    0x1f,
                        /* DWORD dwBBitMask; */
                        /* DWORD dwVBitMask; */
                        /* DWORD dwStencilBitMask; */
                        /* DWORD dwBumpLuminanceBitMask; */
                },
                {
                    0,
                        /* DWORD dwRGBAlphaBitMask; */
                        /* DWORD dwYUVAlphaBitMask; */
                        /* DWORD dwLuminanceAlphaBitMask; */
                        /* DWORD dwRGBZBitMask; */
                        /* DWORD dwYUVZBitMask; */
                },
            }, /* DDPIXELFORMAT ddpfPixelFormat; */
            {
                DDSCAPS_BACKBUFFER
                | DDSCAPS_COMPLEX
                | DDSCAPS_FLIP
                | DDSCAPS_FRONTBUFFER
                | DDSCAPS_LOCALVIDMEM
                | DDSCAPS_PRIMARYSURFACE
                | DDSCAPS_VIDEOMEMORY
                | DDSCAPS_VISIBLE /* DWORD dwCaps; */
            } /* DDSCAPS ddsCaps; */
        },
};

#ifdef VBOX_WITH_VIDEOHWACCEL

static void vboxVhwaPopulateOverlayFourccSurfDesc(DDSURFACEDESC *pDesc, uint32_t fourcc)
{
    memset(pDesc, 0, sizeof (DDSURFACEDESC));

    pDesc->dwSize = sizeof (DDSURFACEDESC);
    pDesc->dwFlags = DDSD_CAPS | DDSD_PIXELFORMAT;
    pDesc->ddpfPixelFormat.dwSize = sizeof (DDPIXELFORMAT);
    pDesc->ddpfPixelFormat.dwFlags = DDPF_FOURCC;
    pDesc->ddpfPixelFormat.dwFourCC = fourcc;
    pDesc->ddsCaps.dwCaps = DDSCAPS_BACKBUFFER
            | DDSCAPS_COMPLEX
            | DDSCAPS_FLIP
            | DDSCAPS_FRONTBUFFER
            | DDSCAPS_LOCALVIDMEM
            | DDSCAPS_OVERLAY
            | DDSCAPS_VIDEOMEMORY
            | DDSCAPS_VISIBLE;
}

static bool vboxPixFormatMatch(DDPIXELFORMAT *pFormat1, DDPIXELFORMAT *pFormat2)
{
    return !memcmp(pFormat1, pFormat2, sizeof (DDPIXELFORMAT));
}

HRESULT vboxSurfDescMerge(DDSURFACEDESC *paDescs, uint32_t *pcDescs, uint32_t cMaxDescs, DDSURFACEDESC *pDesc)
{
    uint32_t cDescs = *pcDescs;

    Assert(cMaxDescs >= cDescs);
    Assert(pDesc->dwFlags == (DDSD_CAPS | DDSD_PIXELFORMAT));
    if (pDesc->dwFlags != (DDSD_CAPS | DDSD_PIXELFORMAT))
        return E_INVALIDARG;

    for (uint32_t i = 0; i < cDescs; ++i)
    {
        DDSURFACEDESC *pCur = &paDescs[i];
        if (vboxPixFormatMatch(&pCur->ddpfPixelFormat, &pDesc->ddpfPixelFormat))
        {
            if (pDesc->dwFlags & DDSD_CAPS)
            {
                pCur->dwFlags |= DDSD_CAPS;
                pCur->ddsCaps.dwCaps |= pDesc->ddsCaps.dwCaps;
            }
            return S_OK;
        }
    }

    if (cMaxDescs > cDescs)
    {
        paDescs[cDescs] = *pDesc;
        ++cDescs;
        *pcDescs = cDescs;
        return VINF_SUCCESS;
    }
    return E_FAIL;
}

HRESULT vboxFormatOpsMerge(FORMATOP *paOps, uint32_t *pcOps, uint32_t cMaxOps, FORMATOP *pOp)
{
    uint32_t cOps = *pcOps;

    Assert(cMaxOps >= cOps);

    for (uint32_t i = 0; i < cOps; ++i)
    {
        FORMATOP *pCur = &paOps[i];
        if (pCur->Format == pOp->Format)
        {
            pCur->Operations |= pOp->Operations;
            Assert(pCur->FlipMsTypes == pOp->FlipMsTypes);
            Assert(pCur->BltMsTypes == pOp->BltMsTypes);
            Assert(pCur->PrivateFormatBitCount == pOp->PrivateFormatBitCount);
            return S_OK;
        }
    }

    if (cMaxOps > cOps)
    {
        paOps[cOps] = *pOp;
        ++cOps;
        *pcOps = cOps;
        return VINF_SUCCESS;
    }
    return E_FAIL;
}

HRESULT VBoxDispD3DGlobal2DFormatsInit(PVBOXWDDMDISP_ADAPTER pAdapter)
{
    HRESULT hr = S_OK;
    memset(&pAdapter->D3D, 0, sizeof (pAdapter->D3D));
    memset(&pAdapter->Formats, 0, sizeof (pAdapter->Formats));

    /* just calc the max number of formats */
    uint32_t cFormats = RT_ELEMENTS(gVBoxFormatOpsBase);
    uint32_t cSurfDescs = RT_ELEMENTS(gVBoxSurfDescsBase);
    uint32_t cOverlayFormats = 0;
    for (uint32_t i = 0; i < pAdapter->cHeads; ++i)
    {
        VBOXDISPVHWA_INFO *pVhwa = &pAdapter->aHeads[i].Vhwa;
        if (pVhwa->Settings.fFlags & VBOXVHWA_F_ENABLED)
        {
            cOverlayFormats += pVhwa->Settings.cFormats;
        }
    }

    cFormats += cOverlayFormats;
    cSurfDescs += cOverlayFormats;

    uint32_t cbFormatOps = cFormats * sizeof (FORMATOP);
    cbFormatOps = (cbFormatOps + 7) & ~3;
    /* ensure the surf descs are 8 byte aligned */
    uint32_t offSurfDescs = (cbFormatOps + 7) & ~3;
    uint32_t cbSurfDescs = cSurfDescs * sizeof (DDSURFACEDESC);
    uint32_t cbBuf = offSurfDescs + cbSurfDescs;
    uint8_t* pvBuf = (uint8_t*)RTMemAllocZ(cbBuf);
    if (pvBuf)
    {
        pAdapter->Formats.paFormatOps = (FORMATOP*)pvBuf;
        memcpy ((void*)pAdapter->Formats.paFormatOps , gVBoxFormatOpsBase, sizeof (gVBoxFormatOpsBase));
        pAdapter->Formats.cFormatOps = RT_ELEMENTS(gVBoxFormatOpsBase);

        FORMATOP fo = {D3DDDIFMT_UNKNOWN, 0, 0, 0, 0};
        for (uint32_t i = 0; i < pAdapter->cHeads; ++i)
        {
            VBOXDISPVHWA_INFO *pVhwa = &pAdapter->aHeads[i].Vhwa;
            if (pVhwa->Settings.fFlags & VBOXVHWA_F_ENABLED)
            {
                for (uint32_t j = 0; j < pVhwa->Settings.cFormats; ++j)
                {
                    fo.Format = pVhwa->Settings.aFormats[j];
                    fo.Operations = FORMATOP_OVERLAY;
                    hr = vboxFormatOpsMerge((FORMATOP *)pAdapter->Formats.paFormatOps, &pAdapter->Formats.cFormatOps, cFormats, &fo);
                    if (FAILED(hr))
                    {
                        WARN(("vboxFormatOpsMerge failed, hr 0x%x", hr));
                    }
                }
            }
        }

        pAdapter->Formats.paSurfDescs = (DDSURFACEDESC*)(pvBuf + offSurfDescs);
        memcpy ((void*)pAdapter->Formats.paSurfDescs , gVBoxSurfDescsBase, sizeof (gVBoxSurfDescsBase));
        pAdapter->Formats.cSurfDescs = RT_ELEMENTS(gVBoxSurfDescsBase);

        DDSURFACEDESC sd;
        for (uint32_t i = 0; i < pAdapter->cHeads; ++i)
        {
            VBOXDISPVHWA_INFO *pVhwa = &pAdapter->aHeads[i].Vhwa;
            if (pVhwa->Settings.fFlags & VBOXVHWA_F_ENABLED)
            {
                for (uint32_t j = 0; j < pVhwa->Settings.cFormats; ++j)
                {
                    uint32_t fourcc = vboxWddmFormatToFourcc(pVhwa->Settings.aFormats[j]);
                    if (fourcc)
                    {
                        vboxVhwaPopulateOverlayFourccSurfDesc(&sd, fourcc);
                        hr = vboxSurfDescMerge((DDSURFACEDESC *)pAdapter->Formats.paSurfDescs, &pAdapter->Formats.cSurfDescs, cSurfDescs, &sd);
                        if (FAILED(hr))
                        {
                            WARN(("vboxFormatOpsMerge failed, hr 0x%x", hr));
                        }
                    }
                }
            }
        }
    }
    else
    {
        WARN(("RTMemAllocZ failed"));
        return E_FAIL;
    }
    return S_OK;
}

void VBoxDispD3DGlobal2DFormatsTerm(PVBOXWDDMDISP_ADAPTER pAdapter)
{
    if (pAdapter->Formats.paFormatOps)
    {
        RTMemFree((void *)pAdapter->Formats.paFormatOps);
        pAdapter->Formats.paFormatOps = NULL;
    }
}

#endif

static CRITICAL_SECTION g_VBoxDispD3DGlobalCritSect;
static VBOXWDDMDISP_D3D g_VBoxDispD3DGlobalD3D;
static VBOXWDDMDISP_FORMATS g_VBoxDispD3DGlobalD3DFormats;
static uint32_t g_cVBoxDispD3DGlobalOpens;

void vboxDispD3DGlobalLock()
{
    EnterCriticalSection(&g_VBoxDispD3DGlobalCritSect);
}

void vboxDispD3DGlobalUnlock()
{
    LeaveCriticalSection(&g_VBoxDispD3DGlobalCritSect);
}

void VBoxDispD3DGlobalInit()
{
    g_cVBoxDispD3DGlobalOpens = 0;
    InitializeCriticalSection(&g_VBoxDispD3DGlobalCritSect);
}

void VBoxDispD3DGlobalTerm()
{
    DeleteCriticalSection(&g_VBoxDispD3DGlobalCritSect);
}

#ifndef D3DCAPS2_CANRENDERWINDOWED
#define D3DCAPS2_CANRENDERWINDOWED UINT32_C(0x00080000)
#endif

#ifdef DEBUG
/*
 * Check capabilities reported by wine and log any which are not good enough for a D3D feature level.
 */

#define VBOX_D3D_CHECK_FLAGS(level, field, flags) do { \
        if (((field) & (flags)) != (flags)) \
        { \
            LogRel(("D3D level %s %s flags: 0x%08X -> 0x%08X (missing 0x%08X)\n", #level, #field, (field), (flags), ((field) & (flags)) ^ (flags))); \
        } \
    } while (0)

#define VBOX_D3D_CHECK_VALUE(level, field, value) do { \
        if ((int64_t)(value) >= 0? (field) < (value): (field) > (value)) \
        { \
            LogRel(("D3D level %s %s value: %lld -> %lld\n", #level, #field, (int64_t)(field), (int64_t)(value))); \
        } \
    } while (0)

#define VBOX_D3D_CHECK_VALUE_HEX(level, field, value) do { \
        if ((field) < (value)) \
        { \
            LogRel(("D3D level %s %s value: 0x%08X -> 0x%08X\n", #level, #field, (field), (value))); \
        } \
    } while (0)

void vboxDispCheckCapsLevel(const D3DCAPS9 *pCaps)
{
    /* Misc. */
    VBOX_D3D_CHECK_FLAGS(misc, pCaps->Caps,                     D3DCAPS_READ_SCANLINE);
    VBOX_D3D_CHECK_FLAGS(misc, pCaps->Caps2,                    D3DCAPS2_CANRENDERWINDOWED | D3DCAPS2_CANSHARERESOURCE);
    VBOX_D3D_CHECK_FLAGS(misc, pCaps->DevCaps,                  D3DDEVCAPS_FLOATTLVERTEX
                                                            /*| D3DDEVCAPS_HWVERTEXBUFFER | D3DDEVCAPS_HWINDEXBUFFER |  D3DDEVCAPS_SUBVOLUMELOCK */);
    VBOX_D3D_CHECK_FLAGS(misc, pCaps->PrimitiveMiscCaps,        D3DPMISCCAPS_INDEPENDENTWRITEMASKS /** @todo needs GL_EXT_draw_buffers2 */
                                                              | D3DPMISCCAPS_FOGINFVF
                                                              | D3DPMISCCAPS_SEPARATEALPHABLEND
                                                              | D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS);
    VBOX_D3D_CHECK_FLAGS(misc, pCaps->RasterCaps,               D3DPRASTERCAPS_SUBPIXEL
                                                              | D3DPRASTERCAPS_STIPPLE
                                                              | D3DPRASTERCAPS_ZBIAS
                                                              | D3DPRASTERCAPS_COLORPERSPECTIVE);
    VBOX_D3D_CHECK_FLAGS(misc, pCaps->TextureCaps,              D3DPTEXTURECAPS_TRANSPARENCY
                                                              | D3DPTEXTURECAPS_TEXREPEATNOTSCALEDBYSIZE);
    VBOX_D3D_CHECK_FLAGS(misc, pCaps->TextureAddressCaps,       D3DPTADDRESSCAPS_MIRRORONCE); /** @todo needs GL_ARB_texture_mirror_clamp_to_edge */
    VBOX_D3D_CHECK_FLAGS(misc, pCaps->VolumeTextureAddressCaps, D3DPTADDRESSCAPS_MIRRORONCE); /** @todo needs GL_ARB_texture_mirror_clamp_to_edge */
    VBOX_D3D_CHECK_FLAGS(misc, pCaps->StencilCaps,              D3DSTENCILCAPS_TWOSIDED);
    VBOX_D3D_CHECK_FLAGS(misc, pCaps->DeclTypes,                D3DDTCAPS_FLOAT16_2 | D3DDTCAPS_FLOAT16_4); /** @todo both need GL_ARB_half_float_vertex */
    VBOX_D3D_CHECK_FLAGS(misc, pCaps->VertexTextureFilterCaps,  D3DPTFILTERCAPS_MINFPOINT
                                                              | D3DPTFILTERCAPS_MAGFPOINT);
    VBOX_D3D_CHECK_VALUE(misc, pCaps->GuardBandLeft,  -8192.);
    VBOX_D3D_CHECK_VALUE(misc, pCaps->GuardBandTop,   -8192.);
    VBOX_D3D_CHECK_VALUE(misc, pCaps->GuardBandRight,  8192.);
    VBOX_D3D_CHECK_VALUE(misc, pCaps->GuardBandBottom, 8192.);
    VBOX_D3D_CHECK_VALUE(misc, pCaps->VS20Caps.DynamicFlowControlDepth, 24);
    VBOX_D3D_CHECK_VALUE(misc, pCaps->VS20Caps.NumTemps, D3DVS20_MAX_NUMTEMPS);
    VBOX_D3D_CHECK_VALUE(misc, pCaps->PS20Caps.DynamicFlowControlDepth, 24);
    VBOX_D3D_CHECK_VALUE(misc, pCaps->PS20Caps.NumTemps, D3DVS20_MAX_NUMTEMPS);

    /* 9_1 */
    VBOX_D3D_CHECK_FLAGS(9.1, pCaps->Caps2,                 D3DCAPS2_DYNAMICTEXTURES | D3DCAPS2_FULLSCREENGAMMA);
    VBOX_D3D_CHECK_FLAGS(9.1, pCaps->PresentationIntervals, D3DPRESENT_INTERVAL_IMMEDIATE | D3DPRESENT_INTERVAL_ONE);
    VBOX_D3D_CHECK_FLAGS(9.1, pCaps->PrimitiveMiscCaps,     D3DPMISCCAPS_COLORWRITEENABLE);
    VBOX_D3D_CHECK_FLAGS(9.1, pCaps->ShadeCaps,             D3DPSHADECAPS_ALPHAGOURAUDBLEND | D3DPSHADECAPS_COLORGOURAUDRGB
                                                          | D3DPSHADECAPS_FOGGOURAUD | D3DPSHADECAPS_SPECULARGOURAUDRGB);
    VBOX_D3D_CHECK_FLAGS(9.1, pCaps->TextureFilterCaps,     D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MINFPOINT
                                                          | D3DPTFILTERCAPS_MAGFLINEAR | D3DPTFILTERCAPS_MAGFPOINT);
    VBOX_D3D_CHECK_FLAGS(9.1, pCaps->TextureCaps,           D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_CUBEMAP
                                                          | D3DPTEXTURECAPS_MIPMAP | D3DPTEXTURECAPS_PERSPECTIVE);
    VBOX_D3D_CHECK_FLAGS(9.1, pCaps->TextureAddressCaps,    D3DPTADDRESSCAPS_CLAMP | D3DPTADDRESSCAPS_INDEPENDENTUV
                                                          | D3DPTADDRESSCAPS_MIRROR | D3DPTADDRESSCAPS_WRAP);
    VBOX_D3D_CHECK_FLAGS(9.1, pCaps->TextureOpCaps,         D3DTEXOPCAPS_DISABLE | D3DTEXOPCAPS_MODULATE
                                                          | D3DTEXOPCAPS_SELECTARG1 | D3DTEXOPCAPS_SELECTARG2);
    VBOX_D3D_CHECK_FLAGS(9.1, pCaps->SrcBlendCaps,          D3DPBLENDCAPS_INVDESTALPHA | D3DPBLENDCAPS_INVDESTCOLOR
                                                          | D3DPBLENDCAPS_INVSRCALPHA | D3DPBLENDCAPS_ONE
                                                          | D3DPBLENDCAPS_SRCALPHA | D3DPBLENDCAPS_ZERO);
    VBOX_D3D_CHECK_FLAGS(9.1, pCaps->DestBlendCaps,         D3DPBLENDCAPS_ONE | D3DPBLENDCAPS_INVSRCALPHA
                                                          | D3DPBLENDCAPS_INVSRCCOLOR | D3DPBLENDCAPS_SRCALPHA | D3DPBLENDCAPS_ZERO);
    VBOX_D3D_CHECK_FLAGS(9.1, pCaps->StretchRectFilterCaps, D3DPTFILTERCAPS_MAGFLINEAR | D3DPTFILTERCAPS_MAGFPOINT
                                                          | D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MINFPOINT);
    VBOX_D3D_CHECK_FLAGS(9.1, pCaps->ZCmpCaps,              D3DPCMPCAPS_ALWAYS | D3DPCMPCAPS_LESSEQUAL);
    VBOX_D3D_CHECK_FLAGS(9.1, pCaps->RasterCaps,            D3DPRASTERCAPS_DEPTHBIAS | D3DPRASTERCAPS_SLOPESCALEDEPTHBIAS);
    VBOX_D3D_CHECK_FLAGS(9.1, pCaps->StencilCaps,           D3DSTENCILCAPS_TWOSIDED);

    VBOX_D3D_CHECK_VALUE(9.1, pCaps->MaxTextureWidth,         2048);
    VBOX_D3D_CHECK_VALUE(9.1, pCaps->MaxTextureHeight,        2048);
    VBOX_D3D_CHECK_VALUE(9.1, pCaps->NumSimultaneousRTs,      1);
    VBOX_D3D_CHECK_VALUE(9.1, pCaps->MaxSimultaneousTextures, 8);
    VBOX_D3D_CHECK_VALUE(9.1, pCaps->MaxTextureBlendStages,   8);
    VBOX_D3D_CHECK_VALUE_HEX(9.1, pCaps->PixelShaderVersion,  D3DPS_VERSION(2,0));
    VBOX_D3D_CHECK_VALUE(9.1, pCaps->MaxPrimitiveCount,       65535);
    VBOX_D3D_CHECK_VALUE(9.1, pCaps->MaxVertexIndex,          65534);
    VBOX_D3D_CHECK_VALUE(9.1, pCaps->MaxVolumeExtent,         256);
    VBOX_D3D_CHECK_VALUE(9.1, pCaps->MaxTextureRepeat,        128); /* Must be zero, or 128, or greater. */
    VBOX_D3D_CHECK_VALUE(9.1, pCaps->MaxAnisotropy,           2);
    VBOX_D3D_CHECK_VALUE(9.1, pCaps->MaxVertexW,              0.f);

    /* 9_2 */
    VBOX_D3D_CHECK_FLAGS(9.2, pCaps->PrimitiveMiscCaps,     D3DPMISCCAPS_SEPARATEALPHABLEND);
    VBOX_D3D_CHECK_FLAGS(9.2, pCaps->DevCaps2,              D3DDEVCAPS2_VERTEXELEMENTSCANSHARESTREAMOFFSET);
    VBOX_D3D_CHECK_FLAGS(9.2, pCaps->TextureAddressCaps,    D3DPTADDRESSCAPS_MIRRORONCE);
    VBOX_D3D_CHECK_FLAGS(9.2, pCaps->VolumeTextureAddressCaps, D3DPTADDRESSCAPS_MIRRORONCE);
    VBOX_D3D_CHECK_VALUE(9.2, pCaps->MaxTextureWidth,         2048);
    VBOX_D3D_CHECK_VALUE(9.2, pCaps->MaxTextureHeight,        2048);
    VBOX_D3D_CHECK_VALUE(9.2, pCaps->MaxTextureRepeat,        2048); /* Must be zero, or 2048, or greater. */
    VBOX_D3D_CHECK_VALUE_HEX(9.2, pCaps->VertexShaderVersion, D3DVS_VERSION(2,0));
    VBOX_D3D_CHECK_VALUE(9.2, pCaps->MaxAnisotropy,           16);
    VBOX_D3D_CHECK_VALUE(9.2, pCaps->MaxPrimitiveCount,       1048575);
    VBOX_D3D_CHECK_VALUE(9.2, pCaps->MaxVertexIndex,          1048575);
    VBOX_D3D_CHECK_VALUE(9.2, pCaps->MaxVertexW,              10000000000.f);

    /* 9_3 */
    VBOX_D3D_CHECK_FLAGS(9.3, pCaps->PS20Caps.Caps,         D3DPS20CAPS_GRADIENTINSTRUCTIONS);
    VBOX_D3D_CHECK_FLAGS(9.3, pCaps->VS20Caps.Caps,         D3DVS20CAPS_PREDICATION);
    VBOX_D3D_CHECK_FLAGS(9.3, pCaps->PrimitiveMiscCaps,     D3DPMISCCAPS_INDEPENDENTWRITEMASKS | D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING);
    VBOX_D3D_CHECK_FLAGS(9.3, pCaps->TextureAddressCaps,    D3DPTADDRESSCAPS_BORDER);
    VBOX_D3D_CHECK_VALUE(9.3, pCaps->MaxTextureWidth,         4096);
    VBOX_D3D_CHECK_VALUE(9.3, pCaps->MaxTextureHeight,        4096);
    VBOX_D3D_CHECK_VALUE(9.3, pCaps->MaxTextureRepeat,        8192); /* Must be zero, or 8192, or greater. */
    VBOX_D3D_CHECK_VALUE(9.3, pCaps->NumSimultaneousRTs,      4);
    VBOX_D3D_CHECK_VALUE(9.3, pCaps->PS20Caps.NumInstructionSlots, 512); /* (Pixel Shader Version 2b) */
    VBOX_D3D_CHECK_VALUE(9.3, pCaps->PS20Caps.NumTemps,       32); /* (Pixel Shader Version 2b) */
    VBOX_D3D_CHECK_VALUE(9.3, pCaps->VS20Caps.NumTemps,       32); /* (Vertex Shader Version 2a) */
    VBOX_D3D_CHECK_VALUE(9.3, pCaps->VS20Caps.StaticFlowControlDepth, 4);
    VBOX_D3D_CHECK_VALUE(9.3, pCaps->MaxVertexShaderConst,    256); /* (Vertex Shader Version 2a); */
    VBOX_D3D_CHECK_VALUE(9.3, pCaps->MaxVertexShader30InstructionSlots, 512);
    VBOX_D3D_CHECK_VALUE_HEX(9.3, pCaps->VertexShaderVersion, D3DVS_VERSION(3,0));

    LogRel(("Capabilities check completed\n"));
}

#undef VBOX_D3D_CHECK_FLAGS
#undef VBOX_D3D_CHECK_VALUE
#undef VBOX_D3D_CHECK_VALUE_HEX

#endif /* DEBUG */

#ifdef VBOX_WITH_MESA3D
HRESULT GaWddmD3DBackendOpen(PVBOXWDDMDISP_D3D pD3D, VBOXWDDM_QAI const *pAdapterInfo, PVBOXWDDMDISP_FORMATS pFormats);
#endif

static HRESULT vboxDispD3DGlobalDoOpen(PVBOXWDDMDISP_D3D pD3D, VBOXWDDM_QAI const *pAdapterInfo, PVBOXWDDMDISP_FORMATS pFormats)
{
    memset(pD3D, 0, sizeof (*pD3D));

    HRESULT hr;
    if (pAdapterInfo->enmHwType == VBOXVIDEO_HWTYPE_VBOX)
    {
        hr = E_FAIL;
    }
#ifdef VBOX_WITH_MESA3D
    else if (pAdapterInfo->enmHwType == VBOXVIDEO_HWTYPE_VMSVGA)
        hr = GaWddmD3DBackendOpen(pD3D, pAdapterInfo, pFormats);
#endif
    else
    {
        RT_NOREF(pFormats);
        hr = E_FAIL;
    }

    if (SUCCEEDED(hr))
    {
        pD3D->cMaxSimRTs = pD3D->Caps.NumSimultaneousRTs;

        Assert(pD3D->cMaxSimRTs);
        Assert(pD3D->cMaxSimRTs < UINT32_MAX/2);

        LOG(("SUCCESS 3D Enabled, pD3D (0x%p)", pD3D));
    }

    return hr;
}

HRESULT VBoxDispD3DGlobalOpen(PVBOXWDDMDISP_D3D pD3D, PVBOXWDDMDISP_FORMATS pFormats, VBOXWDDM_QAI const *pAdapterInfo)
{
    vboxDispD3DGlobalLock();
    if (!g_cVBoxDispD3DGlobalOpens)
    {
        HRESULT hr = vboxDispD3DGlobalDoOpen(&g_VBoxDispD3DGlobalD3D, pAdapterInfo, &g_VBoxDispD3DGlobalD3DFormats);
        if (!SUCCEEDED(hr))
        {
            vboxDispD3DGlobalUnlock();
            WARN(("vboxDispD3DGlobalDoOpen failed hr = 0x%x", hr));
            return hr;
        }
    }
    ++g_cVBoxDispD3DGlobalOpens;
    vboxDispD3DGlobalUnlock();

    *pD3D = g_VBoxDispD3DGlobalD3D;
    *pFormats = g_VBoxDispD3DGlobalD3DFormats;
    return S_OK;
}

void VBoxDispD3DGlobalClose(PVBOXWDDMDISP_D3D pD3D, PVBOXWDDMDISP_FORMATS pFormats)
{
    RT_NOREF(pD3D, pFormats);
    vboxDispD3DGlobalLock();
    --g_cVBoxDispD3DGlobalOpens;
    if (!g_cVBoxDispD3DGlobalOpens)
        g_VBoxDispD3DGlobalD3D.pfnD3DBackendClose(&g_VBoxDispD3DGlobalD3D);
    vboxDispD3DGlobalUnlock();
}
