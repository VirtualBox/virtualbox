/* $Id: VBoxD3DIf.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxVideo Display D3D User mode dll
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#include "VBoxDispD3DCmn.h"

/* DDI2D3D */

D3DFORMAT vboxDDI2D3DFormat(D3DDDIFORMAT format)
{
    /** @todo check they are all equal */
    return (D3DFORMAT)format;
}

D3DMULTISAMPLE_TYPE vboxDDI2D3DMultiSampleType(D3DDDIMULTISAMPLE_TYPE enmType)
{
    /** @todo check they are all equal */
    return (D3DMULTISAMPLE_TYPE)enmType;
}

D3DPOOL vboxDDI2D3DPool(D3DDDI_POOL enmPool)
{
    /** @todo check they are all equal */
    switch (enmPool)
    {
    case D3DDDIPOOL_SYSTEMMEM:
        return D3DPOOL_SYSTEMMEM;
    case D3DDDIPOOL_VIDEOMEMORY:
    case D3DDDIPOOL_LOCALVIDMEM:
    case D3DDDIPOOL_NONLOCALVIDMEM:
        /** @todo what would be proper here? */
        return D3DPOOL_DEFAULT;
    default:
        Assert(0);
    }
    return D3DPOOL_DEFAULT;
}

D3DRENDERSTATETYPE vboxDDI2D3DRenderStateType(D3DDDIRENDERSTATETYPE enmType)
{
    /** @todo not entirely correct, need to check */
    return (D3DRENDERSTATETYPE)enmType;
}

VBOXWDDMDISP_TSS_LOOKUP vboxDDI2D3DTestureStageStateType(D3DDDITEXTURESTAGESTATETYPE enmType)
{
    static const VBOXWDDMDISP_TSS_LOOKUP lookup[] =
    {
        {FALSE, D3DTSS_FORCE_DWORD},             /*  0, D3DDDITSS_TEXTUREMAP */
        {FALSE, D3DTSS_COLOROP},                 /*  1, D3DDDITSS_COLOROP */
        {FALSE, D3DTSS_COLORARG1},               /*  2, D3DDDITSS_COLORARG1 */
        {FALSE, D3DTSS_COLORARG2},               /*  3, D3DDDITSS_COLORARG2 */
        {FALSE, D3DTSS_ALPHAOP},                 /*  4, D3DDDITSS_ALPHAOP */
        {FALSE, D3DTSS_ALPHAARG1},               /*  5, D3DDDITSS_ALPHAARG1 */
        {FALSE, D3DTSS_ALPHAARG2},               /*  6, D3DDDITSS_ALPHAARG2 */
        {FALSE, D3DTSS_BUMPENVMAT00},            /*  7, D3DDDITSS_BUMPENVMAT00 */
        {FALSE, D3DTSS_BUMPENVMAT01},            /*  8, D3DDDITSS_BUMPENVMAT01 */
        {FALSE, D3DTSS_BUMPENVMAT10},            /*  9, D3DDDITSS_BUMPENVMAT10 */
        {FALSE, D3DTSS_BUMPENVMAT11},            /* 10, D3DDDITSS_BUMPENVMAT11 */
        {FALSE, D3DTSS_TEXCOORDINDEX},           /* 11, D3DDDITSS_TEXCOORDINDEX */
        {FALSE, D3DTSS_FORCE_DWORD},             /* 12, unused */
        {TRUE, D3DSAMP_ADDRESSU},                /* 13, D3DDDITSS_ADDRESSU */
        {TRUE, D3DSAMP_ADDRESSV},                /* 14, D3DDDITSS_ADDRESSV */
        {TRUE, D3DSAMP_BORDERCOLOR},             /* 15, D3DDDITSS_BORDERCOLOR */
        {TRUE, D3DSAMP_MAGFILTER},               /* 16, D3DDDITSS_MAGFILTER */
        {TRUE, D3DSAMP_MINFILTER},               /* 17, D3DDDITSS_MINFILTER */
        {TRUE, D3DSAMP_MIPFILTER},               /* 18, D3DDDITSS_MIPFILTER */
        {TRUE, D3DSAMP_MIPMAPLODBIAS},           /* 19, D3DDDITSS_MIPMAPLODBIAS */
        {TRUE, D3DSAMP_MAXMIPLEVEL},             /* 20, D3DDDITSS_MAXMIPLEVEL */
        {TRUE, D3DSAMP_MAXANISOTROPY},           /* 21, D3DDDITSS_MAXANISOTROPY */
        {FALSE, D3DTSS_BUMPENVLSCALE},           /* 22, D3DDDITSS_BUMPENVLSCALE */
        {FALSE, D3DTSS_BUMPENVLOFFSET},          /* 23, D3DDDITSS_BUMPENVLOFFSET */
        {FALSE, D3DTSS_TEXTURETRANSFORMFLAGS},   /* 24, D3DDDITSS_TEXTURETRANSFORMFLAGS */
        {TRUE, D3DSAMP_ADDRESSW},                /* 25, D3DDDITSS_ADDRESSW */
        {FALSE, D3DTSS_COLORARG0},               /* 26, D3DDDITSS_COLORARG0 */
        {FALSE, D3DTSS_ALPHAARG0},               /* 27, D3DDDITSS_ALPHAARG0 */
        {FALSE, D3DTSS_RESULTARG},               /* 28, D3DDDITSS_RESULTARG */
        {TRUE, D3DSAMP_SRGBTEXTURE},             /* 29, D3DDDITSS_SRGBTEXTURE */
        {TRUE, D3DSAMP_ELEMENTINDEX},            /* 30, D3DDDITSS_ELEMENTINDEX */
        {TRUE, D3DSAMP_DMAPOFFSET},              /* 31, D3DDDITSS_DMAPOFFSET */
        {FALSE, D3DTSS_CONSTANT},                /* 32, D3DDDITSS_CONSTANT */
        {FALSE, D3DTSS_FORCE_DWORD},             /* 33, D3DDDITSS_DISABLETEXTURECOLORKEY */
        {FALSE, D3DTSS_FORCE_DWORD},             /* 34, D3DDDITSS_TEXTURECOLORKEYVAL */
    };

    Assert(enmType > 0);
    Assert(enmType < RT_ELEMENTS(lookup));
    Assert(lookup[enmType].dType != D3DTSS_FORCE_DWORD);

    return lookup[enmType];
}

DWORD vboxDDI2D3DUsage(D3DDDI_RESOURCEFLAGS fFlags)
{
    DWORD fUsage = 0;
    if (fFlags.Dynamic)
        fUsage |= D3DUSAGE_DYNAMIC;
    if (fFlags.AutogenMipmap)
        fUsage |= D3DUSAGE_AUTOGENMIPMAP;
    if (fFlags.DMap)
        fUsage |= D3DUSAGE_DMAP;
    if (fFlags.WriteOnly)
        fUsage |= D3DUSAGE_WRITEONLY;
    if (fFlags.NPatches)
        fUsage |= D3DUSAGE_NPATCHES;
    if (fFlags.Points)
        fUsage |= D3DUSAGE_POINTS;
    if (fFlags.RenderTarget)
        fUsage |= D3DUSAGE_RENDERTARGET;
    if (fFlags.RtPatches)
        fUsage |= D3DUSAGE_RTPATCHES;
    if (fFlags.TextApi)
        fUsage |= D3DUSAGE_TEXTAPI;
    if (fFlags.WriteOnly)
        fUsage |= D3DUSAGE_WRITEONLY;
    //below are wddm 1.1-specific
//    if (fFlags.RestrictedContent)
//        fUsage |= D3DUSAGE_RESTRICTED_CONTENT;
//    if (fFlags.RestrictSharedAccess)
//        fUsage |= D3DUSAGE_RESTRICT_SHARED_RESOURCE;
    return fUsage;
}

DWORD vboxDDI2D3DLockFlags(D3DDDI_LOCKFLAGS fLockFlags)
{
    DWORD fFlags = 0;
    if (fLockFlags.Discard)
        fFlags |= D3DLOCK_DISCARD;
    if (fLockFlags.NoOverwrite)
        fFlags |= D3DLOCK_NOOVERWRITE;
    if (fLockFlags.ReadOnly)
        fFlags |= D3DLOCK_READONLY;
    if (fLockFlags.DoNotWait)
        fFlags |= D3DLOCK_DONOTWAIT;
    return fFlags;
}

D3DTEXTUREFILTERTYPE vboxDDI2D3DBltFlags(D3DDDI_BLTFLAGS fFlags)
{
    if (fFlags.Point)
    {
        /* no flags other than [Begin|Continue|End]PresentToDwm are set */
        Assert((fFlags.Value & (~(0x00000100 | 0x00000200 | 0x00000400))) == 1);
        return D3DTEXF_POINT;
    }
    if (fFlags.Linear)
    {
        /* no flags other than [Begin|Continue|End]PresentToDwm are set */
        Assert((fFlags.Value & (~(0x00000100 | 0x00000200 | 0x00000400))) == 2);
        return D3DTEXF_LINEAR;
    }
    /* no flags other than [Begin|Continue|End]PresentToDwm are set */
    Assert((fFlags.Value & (~(0x00000100 | 0x00000200 | 0x00000400))) == 0);
    return D3DTEXF_NONE;
}

D3DQUERYTYPE vboxDDI2D3DQueryType(D3DDDIQUERYTYPE enmType)
{
    return (D3DQUERYTYPE)enmType;
}

DWORD vboxDDI2D3DIssueQueryFlags(D3DDDI_ISSUEQUERYFLAGS Flags)
{
    DWORD fFlags = 0;
    if (Flags.Begin)
        fFlags |= D3DISSUE_BEGIN;
    if (Flags.End)
        fFlags |= D3DISSUE_END;
    return fFlags;
}

void VBoxD3DIfLockUnlockMemSynch(PVBOXWDDMDISP_ALLOCATION pAlloc, D3DLOCKED_RECT *pLockInfo, RECT *pRect, bool bToLockInfo)
{
    Assert(pAlloc->SurfDesc.pitch);
    Assert(pAlloc->pvMem);

    if (!pRect)
    {
        if (pAlloc->SurfDesc.pitch == (UINT)pLockInfo->Pitch)
        {
            Assert(pAlloc->SurfDesc.cbSize);
            if (bToLockInfo)
                memcpy(pLockInfo->pBits, pAlloc->pvMem, pAlloc->SurfDesc.cbSize);
            else
                memcpy(pAlloc->pvMem, pLockInfo->pBits, pAlloc->SurfDesc.cbSize);
        }
        else
        {
            uint8_t *pvSrc, *pvDst;
            uint32_t srcPitch, dstPitch;
            if (bToLockInfo)
            {
                pvSrc = (uint8_t *)pAlloc->pvMem;
                pvDst = (uint8_t *)pLockInfo->pBits;
                srcPitch = pAlloc->SurfDesc.pitch;
                dstPitch = pLockInfo->Pitch;
            }
            else
            {
                pvDst = (uint8_t *)pAlloc->pvMem;
                pvSrc = (uint8_t *)pLockInfo->pBits;
                dstPitch = pAlloc->SurfDesc.pitch;
                srcPitch = (uint32_t)pLockInfo->Pitch;
            }

            uint32_t cRows = vboxWddmCalcNumRows(0, pAlloc->SurfDesc.height, pAlloc->SurfDesc.format);
            uint32_t pitch = RT_MIN(srcPitch, dstPitch);
            Assert(pitch);
            for (UINT j = 0; j < cRows; ++j)
            {
                memcpy(pvDst, pvSrc, pitch);
                pvSrc += srcPitch;
                pvDst += dstPitch;
            }
        }
    }
    else
    {
        uint8_t *pvSrc, *pvDst;
        uint32_t srcPitch, dstPitch;
        uint8_t * pvAllocMemStart = (uint8_t *)pAlloc->pvMem;
        uint32_t offAllocMemStart = vboxWddmCalcOffXYrd(pRect->left, pRect->top, pAlloc->SurfDesc.pitch, pAlloc->SurfDesc.format);
        pvAllocMemStart += offAllocMemStart;

        if (bToLockInfo)
        {
            pvSrc = (uint8_t *)pvAllocMemStart;
            pvDst = (uint8_t *)pLockInfo->pBits;
            srcPitch = pAlloc->SurfDesc.pitch;
            dstPitch = pLockInfo->Pitch;
        }
        else
        {
            pvDst = (uint8_t *)pvAllocMemStart;
            pvSrc = (uint8_t *)pLockInfo->pBits;
            dstPitch = pAlloc->SurfDesc.pitch;
            srcPitch = (uint32_t)pLockInfo->Pitch;
        }

        if (pRect->right - pRect->left == (LONG)pAlloc->SurfDesc.width && srcPitch == dstPitch)
        {
            uint32_t cbSize = vboxWddmCalcSize(pAlloc->SurfDesc.pitch, pRect->bottom - pRect->top, pAlloc->SurfDesc.format);
            memcpy(pvDst, pvSrc, cbSize);
        }
        else
        {
            uint32_t pitch = RT_MIN(srcPitch, dstPitch);
            uint32_t cbCopyLine = vboxWddmCalcRowSize(pRect->left, pRect->right, pAlloc->SurfDesc.format);
            Assert(pitch); NOREF(pitch);
            uint32_t cRows = vboxWddmCalcNumRows(pRect->top, pRect->bottom, pAlloc->SurfDesc.format);
            for (UINT j = 0; j < cRows; ++j)
            {
                memcpy(pvDst, pvSrc, cbCopyLine);
                pvSrc += srcPitch;
                pvDst += dstPitch;
            }
        }
    }
}

HRESULT VBoxD3DIfLockRect(PVBOXWDDMDISP_RESOURCE pRc, UINT iAlloc,
        D3DLOCKED_RECT * pLockedRect,
        CONST RECT *pRect,
        DWORD fLockFlags)
{
    HRESULT hr = E_FAIL;
    Assert(!pRc->aAllocations[iAlloc].LockInfo.cLocks);
    Assert(pRc->cAllocations > iAlloc);
    switch (pRc->aAllocations[0].enmD3DIfType)
    {
        case VBOXDISP_D3DIFTYPE_SURFACE:
        {
            IDirect3DSurface9 *pD3DIfSurf = (IDirect3DSurface9*)pRc->aAllocations[iAlloc].pD3DIf;
            Assert(pD3DIfSurf);
            hr = pD3DIfSurf->LockRect(pLockedRect, pRect, fLockFlags);
            Assert(hr == S_OK);
            break;
        }
        case VBOXDISP_D3DIFTYPE_TEXTURE:
        {
            IDirect3DTexture9 *pD3DIfTex = (IDirect3DTexture9*)pRc->aAllocations[0].pD3DIf;
            Assert(pD3DIfTex);
            hr = pD3DIfTex->LockRect(iAlloc, pLockedRect, pRect, fLockFlags);
            Assert(hr == S_OK);
            break;
        }
        case VBOXDISP_D3DIFTYPE_CUBE_TEXTURE:
        {
            IDirect3DCubeTexture9 *pD3DIfCubeTex = (IDirect3DCubeTexture9*)pRc->aAllocations[0].pD3DIf;
            Assert(pD3DIfCubeTex);
            hr = pD3DIfCubeTex->LockRect(VBOXDISP_CUBEMAP_INDEX_TO_FACE(pRc, iAlloc),
                    VBOXDISP_CUBEMAP_INDEX_TO_LEVEL(pRc, iAlloc), pLockedRect, pRect, fLockFlags);
            Assert(hr == S_OK);
            break;
        }
        case VBOXDISP_D3DIFTYPE_VERTEXBUFFER:
        {
            IDirect3DVertexBuffer9 *pD3D9VBuf = (IDirect3DVertexBuffer9*)pRc->aAllocations[iAlloc].pD3DIf;
            Assert(pD3D9VBuf);
            hr = pD3D9VBuf->Lock(pRect ? pRect->left : 0/* offset */,
                    pRect ? pRect->right : 0 /* size 2 lock - 0 means all */,
                    &pLockedRect->pBits, fLockFlags);
            Assert(hr == S_OK);
            pLockedRect->Pitch = pRc->aAllocations[iAlloc].SurfDesc.pitch;
            break;
        }
        case VBOXDISP_D3DIFTYPE_INDEXBUFFER:
        {
            IDirect3DIndexBuffer9 *pD3D9IBuf = (IDirect3DIndexBuffer9*)pRc->aAllocations[iAlloc].pD3DIf;
            Assert(pD3D9IBuf);
            hr = pD3D9IBuf->Lock(pRect ? pRect->left : 0/* offset */,
                    pRect ? pRect->right : 0 /* size 2 lock - 0 means all */,
                    &pLockedRect->pBits, fLockFlags);
            Assert(hr == S_OK);
            pLockedRect->Pitch = pRc->aAllocations[iAlloc].SurfDesc.pitch;
            break;
        }
        default:
            WARN(("uknown if type %d", pRc->aAllocations[0].enmD3DIfType));
            break;
    }
    return hr;
}

HRESULT VBoxD3DIfUnlockRect(PVBOXWDDMDISP_RESOURCE pRc, UINT iAlloc)
{
    HRESULT hr = S_OK;
    Assert(pRc->cAllocations > iAlloc);
    switch (pRc->aAllocations[0].enmD3DIfType)
    {
        case VBOXDISP_D3DIFTYPE_SURFACE:
        {
            IDirect3DSurface9 *pD3DIfSurf = (IDirect3DSurface9*)pRc->aAllocations[iAlloc].pD3DIf;
            Assert(pD3DIfSurf);
            hr = pD3DIfSurf->UnlockRect();
            Assert(hr == S_OK);
            break;
        }
        case VBOXDISP_D3DIFTYPE_TEXTURE:
        {
            IDirect3DTexture9 *pD3DIfTex = (IDirect3DTexture9*)pRc->aAllocations[0].pD3DIf;
            Assert(pD3DIfTex);
            hr = pD3DIfTex->UnlockRect(iAlloc);
            Assert(hr == S_OK);
            break;
        }
        case VBOXDISP_D3DIFTYPE_CUBE_TEXTURE:
        {
            IDirect3DCubeTexture9 *pD3DIfCubeTex = (IDirect3DCubeTexture9*)pRc->aAllocations[0].pD3DIf;
            Assert(pD3DIfCubeTex);
            hr = pD3DIfCubeTex->UnlockRect(VBOXDISP_CUBEMAP_INDEX_TO_FACE(pRc, iAlloc),
                    VBOXDISP_CUBEMAP_INDEX_TO_LEVEL(pRc, iAlloc));
            Assert(hr == S_OK);
            break;
        }
        case VBOXDISP_D3DIFTYPE_VERTEXBUFFER:
        {
            IDirect3DVertexBuffer9 *pD3D9VBuf = (IDirect3DVertexBuffer9*)pRc->aAllocations[iAlloc].pD3DIf;
            Assert(pD3D9VBuf);
            hr = pD3D9VBuf->Unlock();
            Assert(hr == S_OK);
            break;
        }
        case VBOXDISP_D3DIFTYPE_INDEXBUFFER:
        {
            IDirect3DIndexBuffer9 *pD3D9IBuf = (IDirect3DIndexBuffer9*)pRc->aAllocations[iAlloc].pD3DIf;
            Assert(pD3D9IBuf);
            hr = pD3D9IBuf->Unlock();
            Assert(hr == S_OK);
            break;
        }
        default:
            WARN(("uknown if type %d", pRc->aAllocations[0].enmD3DIfType));
            hr = E_FAIL;
            break;
    }
    return hr;
}
