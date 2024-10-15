/* $Id: GaWddm.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * WDDM helpers implemented for the Gallium based driver.
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
#include "../VBoxDispD3DCmn.h"


HRESULT GaD3DResourceLockRect(PVBOXWDDMDISP_RESOURCE pRc, UINT iAlloc,
                              D3DLOCKED_RECT *pLockedRect,
                              const RECT *pRect,
                              DWORD dwLockFlags)
{
    HRESULT hr;
    Assert(pRc->cAllocations > iAlloc);

    VBOXWDDMDISP_ALLOCATION *pAllocation = &pRc->aAllocations[iAlloc];
    Assert(pAllocation->pD3DIf);

    const VBOXDISP_D3DIFTYPE enmD3DIfType = pAllocation->enmD3DIfType;
    switch (enmD3DIfType)
    {
        case VBOXDISP_D3DIFTYPE_SURFACE:
        {
            Assert(!pAllocation->LockInfo.cLocks);
            IDirect3DSurface9 *pD3DIfSurf = (IDirect3DSurface9 *)pAllocation->pD3DIf;
            hr = pD3DIfSurf->LockRect(pLockedRect, pRect, dwLockFlags);
            Assert(hr == S_OK);
            break;
        }
        case VBOXDISP_D3DIFTYPE_TEXTURE:
        {
            Assert(!pAllocation->LockInfo.cLocks);
            IDirect3DTexture9 *pD3DIfTex = (IDirect3DTexture9 *)pAllocation->pD3DIf;
            hr = pD3DIfTex->LockRect(iAlloc, pLockedRect, pRect, dwLockFlags);
            Assert(hr == S_OK);
            break;
        }
        case VBOXDISP_D3DIFTYPE_CUBE_TEXTURE:
        {
            Assert(!pAllocation->LockInfo.cLocks);
            IDirect3DCubeTexture9 *pD3DIfCubeTex = (IDirect3DCubeTexture9 *)pAllocation->pD3DIf;
            hr = pD3DIfCubeTex->LockRect(VBOXDISP_CUBEMAP_INDEX_TO_FACE(pRc, iAlloc),
                                         VBOXDISP_CUBEMAP_INDEX_TO_LEVEL(pRc, iAlloc),
                                         pLockedRect, pRect, dwLockFlags);
            Assert(hr == S_OK);
            break;
        }
        case VBOXDISP_D3DIFTYPE_VERTEXBUFFER:
        {
            IDirect3DVertexBuffer9 *pD3D9VBuf = (IDirect3DVertexBuffer9 *)pAllocation->pD3DIf;
            const UINT OffsetToLock = pRect ? pRect->left : 0;
            const UINT SizeToLock  = pRect ? pRect->right - pRect->left : 0; /* 0 means all */
            hr = pD3D9VBuf->Lock(OffsetToLock, SizeToLock, &pLockedRect->pBits, dwLockFlags);
            AssertBreak(hr == S_OK);
            pLockedRect->Pitch = pAllocation->SurfDesc.pitch;
            break;
        }
        case VBOXDISP_D3DIFTYPE_INDEXBUFFER:
        {
            IDirect3DIndexBuffer9 *pD3D9IBuf = (IDirect3DIndexBuffer9 *)pAllocation->pD3DIf;
            const UINT OffsetToLock = pRect ? pRect->left : 0;
            const UINT SizeToLock  = pRect ? pRect->right - pRect->left : 0; /* 0 means all */
            hr = pD3D9IBuf->Lock(OffsetToLock, SizeToLock, &pLockedRect->pBits, dwLockFlags);
            AssertBreak(hr == S_OK);
            pLockedRect->Pitch = pAllocation->SurfDesc.pitch;
            break;
        }
        default:
            WARN(("Unknown if type %d", enmD3DIfType));
            hr = E_FAIL;
            break;
    }
    return hr;
}

HRESULT GaD3DResourceUnlockRect(PVBOXWDDMDISP_RESOURCE pRc, UINT iAlloc)
{
    HRESULT hr;
    Assert(pRc->cAllocations > iAlloc);

    VBOXWDDMDISP_ALLOCATION *pAllocation = &pRc->aAllocations[iAlloc];
    Assert(pAllocation->pD3DIf);

    const VBOXDISP_D3DIFTYPE enmD3DIfType = pAllocation->enmD3DIfType;
    switch (enmD3DIfType)
    {
        case VBOXDISP_D3DIFTYPE_SURFACE:
        {
            IDirect3DSurface9 *pD3DIfSurf = (IDirect3DSurface9 *)pAllocation->pD3DIf;
            hr = pD3DIfSurf->UnlockRect();
            Assert(hr == S_OK);
            break;
        }
        case VBOXDISP_D3DIFTYPE_TEXTURE:
        {
            IDirect3DTexture9 *pD3DIfTex = (IDirect3DTexture9 *)pAllocation->pD3DIf;
            hr = pD3DIfTex->UnlockRect(iAlloc);
            Assert(hr == S_OK);
            break;
        }
        case VBOXDISP_D3DIFTYPE_CUBE_TEXTURE:
        {
            IDirect3DCubeTexture9 *pD3DIfCubeTex = (IDirect3DCubeTexture9 *)pAllocation->pD3DIf;
            hr = pD3DIfCubeTex->UnlockRect(VBOXDISP_CUBEMAP_INDEX_TO_FACE(pRc, iAlloc),
                                           VBOXDISP_CUBEMAP_INDEX_TO_LEVEL(pRc, iAlloc));
            Assert(hr == S_OK);
            break;
        }
        case VBOXDISP_D3DIFTYPE_VERTEXBUFFER:
        {
            IDirect3DVertexBuffer9 *pD3D9VBuf = (IDirect3DVertexBuffer9 *)pAllocation->pD3DIf;
            hr = pD3D9VBuf->Unlock();
            Assert(hr == S_OK);
            break;
        }
        case VBOXDISP_D3DIFTYPE_INDEXBUFFER:
        {
            IDirect3DIndexBuffer9 *pD3D9IBuf = (IDirect3DIndexBuffer9 *)pAllocation->pD3DIf;
            hr = pD3D9IBuf->Unlock();
            Assert(hr == S_OK);
            break;
        }
        default:
            WARN(("unknown if type %d", enmD3DIfType));
            hr = E_FAIL;
            break;
    }
    return hr;
}

HRESULT GaD3DResourceSynchMem(PVBOXWDDMDISP_RESOURCE pRc, bool fToBackend)
{
    if (pRc->RcDesc.enmPool != D3DDDIPOOL_SYSTEMMEM)
    {
        return S_OK;
    }

    const DWORD dwLockFlags = fToBackend ? D3DLOCK_DISCARD : D3DLOCK_READONLY;

    if (   pRc->aAllocations[0].enmD3DIfType == VBOXDISP_D3DIFTYPE_TEXTURE
        || pRc->aAllocations[0].enmD3DIfType == VBOXDISP_D3DIFTYPE_CUBE_TEXTURE)
    {
        /* Exclude plain textures and cube textures because they actually
         * use (share) the supplied memory buffer (pRc->aAllocations[].pvMem).
         */
        /* do nothing */
    }
    else if (pRc->aAllocations[0].enmD3DIfType == VBOXDISP_D3DIFTYPE_VOLUME_TEXTURE)
    {
        HRESULT hr;
        IDirect3DVolumeTexture9 *pVolTex = (IDirect3DVolumeTexture9 *)pRc->aAllocations[0].pD3DIf;

        UINT Level;
        for (Level = 0; Level < pRc->cAllocations; ++Level)
        {
            PVBOXWDDMDISP_ALLOCATION pAlloc = &pRc->aAllocations[Level];
            Assert(pAlloc->pvMem);

            /* Entire level. */
            D3DBOX box;
            box.Left   = 0;
            box.Top    = 0;
            box.Right  = pAlloc->SurfDesc.width;
            box.Bottom = pAlloc->SurfDesc.height;
            box.Front  = 0;
            box.Back   = pAlloc->SurfDesc.depth;

            D3DLOCKED_BOX lockedVolume;
            hr = pVolTex->LockBox(Level, &lockedVolume, &box, dwLockFlags);
            Assert(hr == S_OK);
            if (SUCCEEDED(hr))
            {
                Assert(lockedVolume.RowPitch > 0);
                const uint32_t cRows = vboxWddmCalcNumRows(0, pAlloc->SurfDesc.height, pAlloc->SurfDesc.format);
                const UINT cbLine = RT_MIN(pAlloc->SurfDesc.pitch, (UINT)lockedVolume.RowPitch);

                const uint8_t *pu8Src;
                int srcRowPitch;
                int srcSlicePitch;
                uint8_t *pu8Dst;
                int dstRowPitch;
                int dstSlicePitch;
                if (fToBackend)
                {
                    pu8Src        = (uint8_t *)pAlloc->pvMem;
                    srcRowPitch   = pAlloc->SurfDesc.pitch;
                    srcSlicePitch = srcRowPitch * cRows;
                    pu8Dst        = (uint8_t *)lockedVolume.pBits;
                    dstRowPitch   = lockedVolume.RowPitch;
                    dstSlicePitch = lockedVolume.SlicePitch;
                }
                else
                {
                    pu8Src = (uint8_t *)lockedVolume.pBits;
                    srcRowPitch   = lockedVolume.RowPitch;
                    srcSlicePitch = lockedVolume.SlicePitch;
                    pu8Dst = (uint8_t *)pAlloc->pvMem;
                    dstRowPitch   = pAlloc->SurfDesc.pitch;
                    dstSlicePitch = srcRowPitch * cRows;
                }

                for (UINT d = 0; d < pAlloc->SurfDesc.depth; ++d)
                {
                    uint8_t *pu8RowDst = pu8Dst;
                    const uint8_t *pu8RowSrc = pu8Src;
                    for (UINT h = 0; h < cRows; ++h)
                    {
                        memcpy(pu8RowDst, pu8RowSrc, cbLine);
                        pu8RowDst += dstRowPitch;
                        pu8RowSrc += srcRowPitch;
                    }
                    pu8Dst += dstSlicePitch;
                    pu8Src += srcSlicePitch;
                }

                hr = pVolTex->UnlockBox(Level);
                Assert(hr == S_OK);
            }
        }
    }
    else
    {
        for (UINT i = 0; i < pRc->cAllocations; ++i)
        {
            D3DLOCKED_RECT Rect;
            HRESULT hr = GaD3DResourceLockRect(pRc, i, &Rect, NULL, dwLockFlags);
            if (FAILED(hr))
            {
                WARN(("GaD3DResourceLockRect failed, hr(0x%x)", hr));
                return hr;
            }

            PVBOXWDDMDISP_ALLOCATION pAlloc = &pRc->aAllocations[i];
            Assert(pAlloc->pvMem);
            Assert(pAlloc->pvMem != Rect.pBits);

            VBoxD3DIfLockUnlockMemSynch(pAlloc, &Rect, NULL, fToBackend);

            hr = GaD3DResourceUnlockRect(pRc, i);
            Assert(SUCCEEDED(hr));
        }
    }
    return S_OK;
}

DWORD GaDDI2D3DUsage(D3DDDI_RESOURCEFLAGS fFlags)
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
    if (fFlags.ZBuffer)
        fUsage |= D3DUSAGE_DEPTHSTENCIL;
    return fUsage;
}

#if 0
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
#endif

HRESULT GaD3DIfCreateForRc(struct VBOXWDDMDISP_RESOURCE *pRc)
{
    AssertReturn(pRc->cAllocations > 0, E_INVALIDARG);

    /* Initialize D3D interface pointers in order to be able to clean up on failure later. */
    for (UINT i = 0; i < pRc->cAllocations; ++i)
    {
        PVBOXWDDMDISP_ALLOCATION pAllocation = &pRc->aAllocations[i];
        pAllocation->enmD3DIfType = VBOXDISP_D3DIFTYPE_UNDEFINED;
        pAllocation->pD3DIf = NULL;
    }

    PVBOXWDDMDISP_DEVICE pDevice = pRc->pDevice;
    IDirect3DDevice9 *pDevice9If = VBOXDISP_D3DEV(pDevice);
    AssertReturn(pDevice9If, E_FAIL);

    HRESULT hr = E_FAIL;

    const DWORD d3dUsage                     = GaDDI2D3DUsage(pRc->RcDesc.fFlags);
    const D3DFORMAT d3dFormat                = vboxDDI2D3DFormat(pRc->RcDesc.enmFormat);
    const D3DPOOL d3dPool                    = vboxDDI2D3DPool(pRc->RcDesc.enmPool);
    const D3DMULTISAMPLE_TYPE d3dMultiSample = vboxDDI2D3DMultiSampleType(pRc->RcDesc.enmMultisampleType);
    const DWORD d3dMultisampleQuality        = pRc->RcDesc.MultisampleQuality;
    const BOOL d3dLockable                   = !pRc->RcDesc.fFlags.NotLockable;

    if (   VBOXWDDMDISP_IS_TEXTURE(pRc->RcDesc.fFlags)
        || pRc->RcDesc.fFlags.VideoProcessRenderTarget
        || pRc->RcDesc.fFlags.DecodeRenderTarget)
    {
        PVBOXWDDMDISP_ALLOCATION pAllocation = &pRc->aAllocations[0];
        IDirect3DBaseTexture9 *pD3DIfTex = NULL;
        VBOXDISP_D3DIFTYPE enmD3DIfType = VBOXDISP_D3DIFTYPE_UNDEFINED;

        if (pRc->RcDesc.fFlags.CubeMap)
        {
            if (   pAllocation->SurfDesc.width != pAllocation->SurfDesc.height
                || pRc->cAllocations % 6 != 0)
            {
                WARN(("unexpected cubemap texture config: %dx%d, allocs: %d",
                      pAllocation->SurfDesc.width, pAllocation->SurfDesc.height, pRc->cAllocations));
                hr = E_INVALIDARG;
            }
            else
            {
                HANDLE *pSharedHandle = NULL;
                if (d3dPool == D3DPOOL_SYSTEMMEM)
                {
                    /* It is expected that allocations are in continous memory blocks. */
                    pSharedHandle = &pRc->aAllocations[0].pvMem;
                    vboxVDbgPrintF((__FUNCTION__" using pvMem %p\n", pRc->aAllocations[0].pvMem));
                }

                hr = pDevice9If->CreateCubeTexture(pAllocation->SurfDesc.d3dWidth,
                                                   VBOXDISP_CUBEMAP_LEVELS_COUNT(pRc),
                                                   d3dUsage, d3dFormat, d3dPool,
                                                   (IDirect3DCubeTexture9**)&pD3DIfTex,
                                                   pSharedHandle);
                Assert(hr == S_OK && pD3DIfTex);
                enmD3DIfType = VBOXDISP_D3DIFTYPE_CUBE_TEXTURE;
            }
        }
        else if (pRc->RcDesc.fFlags.Volume)
        {
            /* D3DUSAGE_DYNAMIC because we have to lock it in GaDdiVolBlt. */
            hr = pDevice9If->CreateVolumeTexture(pAllocation->SurfDesc.d3dWidth,
                                                 pAllocation->SurfDesc.height,
                                                 pAllocation->SurfDesc.depth,
                                                 pRc->cAllocations,
                                                 d3dUsage | D3DUSAGE_DYNAMIC, d3dFormat, d3dPool,
                                                 (IDirect3DVolumeTexture9**)&pD3DIfTex,
                                                 NULL);
            Assert(hr == S_OK && pD3DIfTex);
            enmD3DIfType = VBOXDISP_D3DIFTYPE_VOLUME_TEXTURE;
        }
        else
        {
            HANDLE *pSharedHandle = NULL;
            if (d3dPool == D3DPOOL_SYSTEMMEM)
            {
                /* It is expected that allocations are in continous memory blocks.
                 *
                 * Also Gallium Nine state tracker has a comment which implies this:
                 * "Some apps expect the memory to be allocated in
                 * continous blocks"
                 */
                pSharedHandle = &pRc->aAllocations[0].pvMem;
                vboxVDbgPrintF((__FUNCTION__" using pvMem %p\n", pRc->aAllocations[0].pvMem));
            }

            hr = pDevice9If->CreateTexture(pAllocation->SurfDesc.d3dWidth,
                                           pAllocation->SurfDesc.height,
                                           pRc->cAllocations,
                                           d3dUsage, d3dFormat, d3dPool,
                                           (IDirect3DTexture9**)&pD3DIfTex,
                                           pSharedHandle);
            Assert(hr == S_OK && pD3DIfTex);
            enmD3DIfType = VBOXDISP_D3DIFTYPE_TEXTURE;
        }

        if (SUCCEEDED(hr))
        {
            Assert(pD3DIfTex);
            Assert(enmD3DIfType != VBOXDISP_D3DIFTYPE_UNDEFINED);

            for (UINT i = 0; i < pRc->cAllocations; ++i)
            {
                PVBOXWDDMDISP_ALLOCATION p = &pRc->aAllocations[i];
                p->enmD3DIfType = enmD3DIfType;
                p->pD3DIf = pD3DIfTex;
                if (i > 0)
                    pD3DIfTex->AddRef();
            }
        }
    }
    else if (pRc->RcDesc.fFlags.RenderTarget || pRc->RcDesc.fFlags.Primary)
    {
        Assert(pRc->RcDesc.enmPool != D3DDDIPOOL_SYSTEMMEM);
        for (UINT i = 0; i < pRc->cAllocations; ++i)
        {
            PVBOXWDDMDISP_ALLOCATION pAllocation = &pRc->aAllocations[i];
            IDirect3DSurface9 *pD3D9Surf = NULL;

            if (   pAllocation->enmType == VBOXWDDM_ALLOC_TYPE_UMD_RC_GENERIC
                || pAllocation->enmType == VBOXWDDM_ALLOC_TYPE_STD_SHAREDPRIMARYSURFACE)
            {
                hr = pDevice9If->CreateRenderTarget(pAllocation->SurfDesc.width,
                                                    pAllocation->SurfDesc.height,
                                                    d3dFormat,
                                                    d3dMultiSample,
                                                    d3dMultisampleQuality,
                                                    d3dLockable,
                                                    &pD3D9Surf,
                                                    NULL);
                AssertBreak(SUCCEEDED(hr) && pD3D9Surf);

            }
#ifdef VBOX_WITH_VMSVGA3D_DX9
            else if (pAllocation->enmType == VBOXWDDM_ALLOC_TYPE_D3D)
            {
                hr = pDevice9If->CreateRenderTarget(pAllocation->AllocDesc.surfaceInfo.size.width,
                                                    pAllocation->AllocDesc.surfaceInfo.size.height,
                                                    d3dFormat,
                                                    d3dMultiSample,
                                                    d3dMultisampleQuality,
                                                    d3dLockable,
                                                    &pD3D9Surf,
                                                    NULL);
                AssertBreak(SUCCEEDED(hr) && pD3D9Surf);
            }
#endif
            else
            {
                WARN(("unexpected alloc type %d", pAllocation->enmType));
                hr = E_FAIL;
            }

            pAllocation->enmD3DIfType = VBOXDISP_D3DIFTYPE_SURFACE;
            pAllocation->pD3DIf = pD3D9Surf;
        }
    }
    else if (pRc->RcDesc.fFlags.ZBuffer)
    {
        for (UINT i = 0; i < pRc->cAllocations; ++i)
        {
            PVBOXWDDMDISP_ALLOCATION pAllocation = &pRc->aAllocations[i];
            const BOOL Discard = TRUE;
            IDirect3DSurface9 *pD3D9Surf = NULL;

            hr = pDevice9If->CreateDepthStencilSurface(pAllocation->SurfDesc.width,
                                                       pAllocation->SurfDesc.height,
                                                       d3dFormat,
                                                       d3dMultiSample,
                                                       d3dMultisampleQuality,
                                                       Discard,
                                                       &pD3D9Surf,
                                                       NULL);
            AssertBreak(SUCCEEDED(hr) && pD3D9Surf);

            pAllocation->enmD3DIfType = VBOXDISP_D3DIFTYPE_SURFACE;
            pAllocation->pD3DIf = pD3D9Surf;
        }
    }
    else if (pRc->RcDesc.fFlags.VertexBuffer)
    {
        for (UINT i = 0; i < pRc->cAllocations; ++i)
        {
            PVBOXWDDMDISP_ALLOCATION pAllocation = &pRc->aAllocations[i];
            IDirect3DVertexBuffer9 *pD3D9VBuf = NULL;
            const DWORD d3dFVF = pRc->RcDesc.Fvf;

            /** @todo need for Gallium? avoid using dynamic to ensure wine does not switch do user buffer */
            hr = pDevice9If->CreateVertexBuffer(pAllocation->SurfDesc.width,
                                                d3dUsage & (~D3DUSAGE_DYNAMIC),
                                                d3dFVF,
                                                d3dPool,
                                                &pD3D9VBuf,
                                                NULL);
            AssertBreak(SUCCEEDED(hr) && pD3D9VBuf);

            pAllocation->enmD3DIfType = VBOXDISP_D3DIFTYPE_VERTEXBUFFER;
            pAllocation->pD3DIf = pD3D9VBuf;
        }
    }
    else if (pRc->RcDesc.fFlags.IndexBuffer)
    {
        for (UINT i = 0; i < pRc->cAllocations; ++i)
        {
            PVBOXWDDMDISP_ALLOCATION pAllocation = &pRc->aAllocations[i];
            IDirect3DIndexBuffer9 *pD3D9IBuf = NULL;

            hr = pDevice9If->CreateIndexBuffer(pAllocation->SurfDesc.width,
                                               d3dUsage,
                                               d3dFormat,
                                               d3dPool,
                                               &pD3D9IBuf,
                                               NULL);
            AssertBreak(SUCCEEDED(hr) && pD3D9IBuf);

            pAllocation->enmD3DIfType = VBOXDISP_D3DIFTYPE_INDEXBUFFER;
            pAllocation->pD3DIf = pD3D9IBuf;
        }
    }
    else
    {
        WARN(("unsupported resource flags 0x%x", pRc->RcDesc.fFlags.Value));
        hr = E_FAIL;
    }

    if (SUCCEEDED(hr))
    {
        if (pRc->RcDesc.enmPool == D3DDDIPOOL_SYSTEMMEM)
        {
            /* Copy the content of the supplied memory buffer to the Gallium backend. */
            GaD3DResourceSynchMem(pRc, /*fToBackend=*/ true);
        }
    }
    else
    {
        /* Release every created D3D interface. */
        for (UINT i = 0; i < pRc->cAllocations; ++i)
        {
            PVBOXWDDMDISP_ALLOCATION pAllocation = &pRc->aAllocations[i];
            if (pAllocation->pD3DIf != NULL)
            {
               pAllocation->pD3DIf->Release();
               pAllocation->enmD3DIfType = VBOXDISP_D3DIFTYPE_UNDEFINED;
               pAllocation->pD3DIf = NULL;
            }
        }
    }

    return hr;
}

HRESULT GaD3DIfDeviceCreate(struct VBOXWDDMDISP_DEVICE *pDevice)
{
    Assert(!pDevice->pDevice9If);

    HRESULT hr;
    IGalliumStack *pGalliumStack = pDevice->pAdapter->D3D.pGalliumStack;
    if (pGalliumStack)
    {
        /* Gallium backend does not use the implicit swapchain,
         * therefore the presentation parameters here are sane arbitrary values.
         */
        D3DPRESENT_PARAMETERS pp;
        RT_ZERO(pp);
        pp.BackBufferWidth    = 4;
        pp.BackBufferHeight   = 4;
        pp.BackBufferFormat   = D3DFMT_A8R8G8B8;
        pp.BackBufferCount    = 0;
        pp.MultiSampleType    = D3DMULTISAMPLE_NONE;
        pp.MultiSampleQuality = 0;
        pp.SwapEffect         = D3DSWAPEFFECT_COPY; /* 'nine' creates 1 back buffer for _COPY instead of 2 for _DISCARD. */
        pp.Windowed           = TRUE;

        const DWORD fFlags =  D3DCREATE_HARDWARE_VERTEXPROCESSING
                            | D3DCREATE_FPU_PRESERVE; /* Do not allow to mess with FPU control word. */

        hr = pGalliumStack->GaCreateDeviceEx(D3DDEVTYPE_HAL, 0, fFlags, &pp, 0,
                                       pDevice->pAdapter->hAdapter,
                                       pDevice->hDevice,
                                       &pDevice->RtCallbacks,
                                       &pDevice->pAdapter->AdapterInfo.u.vmsvga.HWInfo,
                                       (IDirect3DDevice9Ex **)&pDevice->pDevice9If);
        if (FAILED(hr))
        {
            WARN(("CreateDevice hr 0x%x", hr));
        }
    }
    else
    {
        WARN(("pGalliumStack is 0"));
        hr = E_FAIL;
    }

    return hr;
}

static int gaD3DIfSetHostId(IGaDirect3DDevice9Ex *pGaD3DDevice9Ex, PVBOXWDDMDISP_ALLOCATION pAlloc, uint32_t hostID, uint32_t *pHostID)
{
    VBOXDISPIFESCAPE_SETALLOCHOSTID SetHostID;
    RT_ZERO(SetHostID);
    SetHostID.EscapeHdr.escapeCode = VBOXESC_SETALLOCHOSTID;
    SetHostID.hostID = hostID;
    SetHostID.hAlloc = pAlloc->hAllocation;

    HRESULT hr = pGaD3DDevice9Ex->EscapeCb(&SetHostID, sizeof(SetHostID), /* fHardwareAccess= */ true);
    if (SUCCEEDED(hr))
    {
        if (pHostID)
            *pHostID = SetHostID.EscapeHdr.u32CmdSpecific;

        return SetHostID.rc;
    }
    else
        WARN(("pfnEscapeCb VBOXESC_SETALLOCHOSTID failed hr 0x%x", hr));

    return VERR_GENERAL_FAILURE;
}

IUnknown *GaD3DIfCreateSharedPrimary(struct VBOXWDDMDISP_ALLOCATION *pAlloc)
{
    /* This allocation has been created in miniport driver DxgkDdiGetStandardAllocationDriverData.
     * Create the corresponding Gallium D3D interface.
     *
     * @todo Consider createing SVGA surface for D3DKMDT_STANDARDALLOCATION_SHAREDPRIMARYSURFACE
     * in miniport and use it as shared sid.
     */
    PVBOXWDDMDISP_RESOURCE pRc = pAlloc->pRc;

    AssertReturn(pAlloc->enmType = VBOXWDDM_ALLOC_TYPE_STD_SHAREDPRIMARYSURFACE, NULL);
    AssertReturn(pRc->RcDesc.fFlags.SharedResource, NULL);
    AssertReturn(pRc->fFlags.Opened && pRc->fFlags.KmResource && !pRc->fFlags.Generic, NULL);

    PVBOXWDDMDISP_DEVICE pDevice = pRc->pDevice;
    IDirect3DDevice9 *pDevice9If = VBOXDISP_D3DEV(pDevice);
    IGaDirect3DDevice9Ex *pGaD3DDevice9Ex = NULL;
    HRESULT hr = pDevice9If->QueryInterface(IID_IGaDirect3DDevice9Ex, (void**)&pGaD3DDevice9Ex);
    if (FAILED(hr))
    {
        WARN(("QueryInterface(IID_IGaDirect3DDevice9Ex), hr 0x%x", hr));
        return NULL;
    }

    /* Create Gallium surface for this process. */
    hr = GaD3DIfCreateForRc(pRc);
    if (FAILED(hr))
    {
        WARN(("GaD3DIfCreateForRc, hr 0x%x", hr));
        return NULL;
    }

    Assert(pAlloc->pD3DIf);
    Assert(pAlloc->enmD3DIfType == VBOXDISP_D3DIFTYPE_SURFACE);

    IDirect3DSurface9 *pSurfIf = NULL;
    hr = VBoxD3DIfSurfGet(pRc, pAlloc->iAlloc, &pSurfIf);
    if (FAILED(hr))
    {
        WARN(("VBoxD3DIfSurfGet hr %#x", hr));
        return NULL;
    }

    /* Must assign the sid to the allocation.
     * Note: sid == hostID, the latter name is used for historical reasons.
     */
    uint32_t hostID = 0;
    hr = pGaD3DDevice9Ex->GaSurfaceId(pSurfIf, &hostID);
    if (SUCCEEDED(hr))
    {
        Assert(hostID);

        /* Remember the allocation sid. */
        pAlloc->hostID = hostID;

        /* Inform miniport that this allocation is associated with the given sid.
         * If the allocation is already associated, the miniport will return the already used sid.
         */
        uint32_t usedHostId = 0;
        int rc = gaD3DIfSetHostId(pGaD3DDevice9Ex, pAlloc, hostID, &usedHostId);
        if (RT_SUCCESS(rc))
        {
            Assert(hostID == usedHostId);

            /* Remember that this sid is used for all operations on this allocation. */
            pAlloc->hSharedHandle = (HANDLE)(uintptr_t)hostID;
        }
        else
        {
            if (rc == VERR_NOT_EQUAL)
            {
                /* The allocation already has an associated sid.
                 * The resource has been already opened by someone else or there is a bug.
                 * In both cases we need a warning, because this is something unusual.
                 */

#ifndef VBOX_WITH_VMSVGA3D_DX9
                WARN(("another hostId %d is in use, using it instead", usedHostId));
#else
                /* This is most likely a _D3D surface, which is used as actual destination of the shared primary. */
#endif

                Assert(hostID != usedHostId);
                Assert(usedHostId);

                /* Remember which sid is actually used for this allocation. */
                pAlloc->hSharedHandle = (HANDLE)(uintptr_t)usedHostId;

                /* Inform the miniport. */
                VBOXDISPIFESCAPE_GASHAREDSID data;
                RT_ZERO(data);
                data.EscapeHdr.escapeCode = VBOXESC_GASHAREDSID;
                data.u32Sid = hostID;
                data.u32SharedSid = usedHostId;
                hr = pGaD3DDevice9Ex->EscapeCb(&data, sizeof(data), /* fHardwareAccess= */ false);
            }
            else
            {
                WARN(("gaD3DIfSetHostId %#x", hr));
                hr = E_FAIL;
            }
        }
    }
    else
    {
        WARN(("GaSurfaceId, hr 0x%x", hr));
    }

    pGaD3DDevice9Ex->Release();

    pSurfIf->Release();

    if (FAILED(hr))
    {
        AssertFailed();
        pAlloc->pD3DIf->Release();
        pAlloc->pD3DIf = NULL;
    }

    return pAlloc->pD3DIf;
}

#ifndef D3DCAPS2_CANRENDERWINDOWED
#define D3DCAPS2_CANRENDERWINDOWED UINT32_C(0x00080000)
#endif

static HRESULT gaWddmGetD3D9Caps(VBOXWDDM_QAI const *pAdapterInfo, IDirect3D9Ex *pD3D9If, D3DCAPS9 *pCaps)
{
    HRESULT hr = pD3D9If->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, pCaps);
    if (FAILED(hr))
    {
        WARN(("GetDeviceCaps failed hr(0x%x)", hr));
        return hr;
    }

#ifdef DEBUG
    vboxDispCheckCapsLevel(pCaps);
#endif

    /*
     * Tweak capabilities which are required for Feature Level 9.3, but
     * not returned by the backend.
     */

    /* (Apparently) needed for Windows Media Player to work properly. */
    pCaps->Caps                     |= D3DCAPS_READ_SCANLINE;
    pCaps->Caps2                    |= D3DCAPS2_CANRENDERWINDOWED |
                                       D3DCAPS2_CANSHARERESOURCE;
    /* "This flag is obsolete but must be set by the driver." */
    pCaps->DevCaps                  |= D3DDEVCAPS_FLOATTLVERTEX;
    pCaps->PrimitiveMiscCaps        |= D3DPMISCCAPS_FOGINFVF |
                                       D3DPMISCCAPS_INDEPENDENTWRITEMASKS;
    pCaps->RasterCaps               |= D3DPRASTERCAPS_SUBPIXEL |
                                       D3DPRASTERCAPS_STIPPLE |
                                       D3DPRASTERCAPS_ZBIAS;
    pCaps->TextureCaps              |= D3DPTEXTURECAPS_TRANSPARENCY |
                                       D3DPTEXTURECAPS_TEXREPEATNOTSCALEDBYSIZE;
    pCaps->TextureAddressCaps       |= D3DPTADDRESSCAPS_MIRRORONCE;
    pCaps->VolumeTextureAddressCaps |= D3DPTADDRESSCAPS_MIRRORONCE;
    pCaps->VertexTextureFilterCaps  |= D3DPTFILTERCAPS_MINFPOINT |
                                       D3DPTFILTERCAPS_MAGFPOINT;


    /* Required for Shader Model 3.0 but not set by Gallium backend. */
    pCaps->PS20Caps.Caps           |= D3DPS20CAPS_NOTEXINSTRUCTIONLIMIT;

    if (RT_BOOL(pAdapterInfo->u32AdapterCaps & VBOXWDDM_QAI_CAP_DXVAHD))
        pCaps->Caps3 |=  D3DCAPS3_DXVAHD;

#ifdef DEBUG
    vboxDispCheckCapsLevel(pCaps);
#endif

    vboxDispDumpD3DCAPS9(pCaps);

    return S_OK;
}

static FORMATOP gGaFormatOps3D[] = {
    {D3DDDIFMT_A8R8G8B8,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        FORMATOP_SAME_FORMAT_UP_TO_ALPHA_RENDERTARGET|
        FORMATOP_CONVERT_TO_ARGB|FORMATOP_OFFSCREENPLAIN|FORMATOP_SRGBREAD|
        FORMATOP_MEMBEROFGROUP_ARGB|
        FORMATOP_SRGBWRITE|FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_X8R8G8B8,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        FORMATOP_DISPLAYMODE|FORMATOP_3DACCELERATION|
        FORMATOP_CONVERT_TO_ARGB|FORMATOP_OFFSCREENPLAIN|FORMATOP_SRGBREAD|
        FORMATOP_MEMBEROFGROUP_ARGB|
        FORMATOP_SRGBWRITE|FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_A2R10G10B10,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_CONVERT_TO_ARGB|FORMATOP_OFFSCREENPLAIN|
        FORMATOP_MEMBEROFGROUP_ARGB|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_X1R5G5B5,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_CONVERT_TO_ARGB|FORMATOP_OFFSCREENPLAIN|
        FORMATOP_MEMBEROFGROUP_ARGB|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_A1R5G5B5,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        FORMATOP_SAME_FORMAT_UP_TO_ALPHA_RENDERTARGET|
        FORMATOP_CONVERT_TO_ARGB|FORMATOP_OFFSCREENPLAIN|
        FORMATOP_MEMBEROFGROUP_ARGB|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_A4R4G4B4,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        FORMATOP_SAME_FORMAT_UP_TO_ALPHA_RENDERTARGET|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_R5G6B5,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        FORMATOP_DISPLAYMODE|FORMATOP_3DACCELERATION|
        FORMATOP_CONVERT_TO_ARGB|FORMATOP_OFFSCREENPLAIN|
        FORMATOP_MEMBEROFGROUP_ARGB|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_L16,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_A8L8,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_A8,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_L8,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_D16,   FORMATOP_TEXTURE|FORMATOP_ZSTENCIL|FORMATOP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH, 0, 0, 0},
    {D3DDDIFMT_D24S8, FORMATOP_TEXTURE|FORMATOP_ZSTENCIL|FORMATOP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH, 0, 0, 0},
    {D3DDDIFMT_D24X8, FORMATOP_TEXTURE|FORMATOP_ZSTENCIL|FORMATOP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH, 0, 0, 0},
    {D3DDDIFMT_D16_LOCKABLE, FORMATOP_ZSTENCIL|FORMATOP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH, 0, 0, 0},
    {D3DDDIFMT_X8D24, FORMATOP_TEXTURE|FORMATOP_ZSTENCIL|FORMATOP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH, 0, 0, 0},
    {D3DDDIFMT_D32F_LOCKABLE, FORMATOP_TEXTURE|FORMATOP_ZSTENCIL|FORMATOP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH, 0, 0, 0},
    {D3DDDIFMT_S8D24, FORMATOP_TEXTURE|FORMATOP_ZSTENCIL|FORMATOP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH, 0, 0, 0},

    {D3DDDIFMT_DXT1,
        FORMATOP_TEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        FORMATOP_OFFSCREENPLAIN|FORMATOP_SRGBREAD|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_DXT2,
        FORMATOP_TEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        FORMATOP_OFFSCREENPLAIN|FORMATOP_SRGBREAD|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_DXT3,
        FORMATOP_TEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        FORMATOP_OFFSCREENPLAIN|FORMATOP_SRGBREAD|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_DXT4,
        FORMATOP_TEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        FORMATOP_OFFSCREENPLAIN|FORMATOP_SRGBREAD|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_DXT5,
        FORMATOP_TEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        FORMATOP_OFFSCREENPLAIN|FORMATOP_SRGBREAD|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_X8L8V8U8,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        0|
        FORMATOP_BUMPMAP|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_A2W10V10U10,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        0|
        FORMATOP_BUMPMAP|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_V8U8,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        0|
        FORMATOP_BUMPMAP|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_Q8W8V8U8,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        0|
        0|
        FORMATOP_OFFSCREENPLAIN|
        FORMATOP_BUMPMAP|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_CxV8U8, FORMATOP_NOFILTER|FORMATOP_NOALPHABLEND|FORMATOP_NOTEXCOORDWRAPNORMIP, 0, 0, 0},

    {D3DDDIFMT_R16F,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_R32F,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_G16R16F,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_G32R32F,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_A16B16G16R16F,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_A32B32G32R32F,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_G16R16,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_A16B16G16R16,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_V16U16,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        0|
        FORMATOP_BUMPMAP|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_P8, FORMATOP_DISPLAYMODE|FORMATOP_3DACCELERATION|FORMATOP_OFFSCREENPLAIN, 0, 0, 0},

    {D3DDDIFMT_UYVY,
        0|
        0|
        0|
        FORMATOP_CONVERT_TO_ARGB|FORMATOP_OFFSCREENPLAIN|
        FORMATOP_NOFILTER|
        FORMATOP_NOALPHABLEND|
        FORMATOP_NOTEXCOORDWRAPNORMIP, 0, 0, 0},

    {D3DDDIFMT_YUY2,
        0|
        0|
        0|
        FORMATOP_CONVERT_TO_ARGB|FORMATOP_OFFSCREENPLAIN|
        FORMATOP_NOFILTER|
        FORMATOP_NOALPHABLEND|
        FORMATOP_NOTEXCOORDWRAPNORMIP, 0, 0, 0},

    {D3DDDIFMT_Q16W16V16U16,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_OFFSCREENPLAIN|
        FORMATOP_BUMPMAP|FORMATOP_DMAP|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_X8B8G8R8,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        FORMATOP_SAME_FORMAT_UP_TO_ALPHA_RENDERTARGET|
        FORMATOP_CONVERT_TO_ARGB|FORMATOP_OFFSCREENPLAIN|FORMATOP_SRGBREAD|
        FORMATOP_DMAP|FORMATOP_MEMBEROFGROUP_ARGB|
        FORMATOP_SRGBWRITE|FORMATOP_AUTOGENMIPMAP|FORMATOP_VERTEXTEXTURE|
        FORMATOP_OVERLAY, 0, 0, 0},

    {D3DDDIFMT_BINARYBUFFER, FORMATOP_OFFSCREENPLAIN, 0, 0, 0},

    {D3DDDIFMT_A4L4,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        FORMATOP_CONVERT_TO_ARGB|FORMATOP_OFFSCREENPLAIN|
        FORMATOP_DMAP|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_A2B10G10R10,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_CONVERT_TO_ARGB|FORMATOP_OFFSCREENPLAIN|
        FORMATOP_DMAP|FORMATOP_MEMBEROFGROUP_ARGB|
        FORMATOP_AUTOGENMIPMAP|FORMATOP_VERTEXTEXTURE, 0, 0, 0},
};

static void gaWddmD3DBackendClose(PVBOXWDDMDISP_D3D pD3D)
{
    pD3D->pGalliumStack->Release();
    pD3D->pGalliumStack = 0;
}

HRESULT GaWddmD3DBackendOpen(PVBOXWDDMDISP_D3D pD3D, VBOXWDDM_QAI const *pAdapterInfo, PVBOXWDDMDISP_FORMATS pFormats)
{
    HRESULT hr = GalliumStackCreate(&pD3D->pGalliumStack);
    if (SUCCEEDED(hr))
    {
        IDirect3D9Ex *pD3D9 = NULL;
        hr = pD3D->pGalliumStack->CreateDirect3DEx(0 /* hAdapter */,
                                                   0 /* hDevice */,
                                                   0 /* pDeviceCallbacks */,
                                                   &pAdapterInfo->u.vmsvga.HWInfo,
                                                   &pD3D9);
        if (SUCCEEDED(hr))
        {
            hr = gaWddmGetD3D9Caps(pAdapterInfo, pD3D9, &pD3D->Caps);
            pD3D9->Release();

            if (SUCCEEDED(hr))
            {
                memset(pFormats, 0, sizeof (*pFormats));
                pFormats->paFormatOps = gGaFormatOps3D;
                pFormats->cFormatOps = RT_ELEMENTS(gGaFormatOps3D);

                pD3D->pfnD3DBackendClose = gaWddmD3DBackendClose;

                return S_OK;
            }

            WARN(("vboxWddmGetD3D9Caps failed hr = 0x%x", hr));
        }
        else
        {
            WARN(("Direct3DCreate9Ex failed hr = 0x%x", hr));
        }

        pD3D->pGalliumStack->Release();
        pD3D->pGalliumStack = 0;
    }
    else
    {
        WARN(("VBoxDispD3DOpen failed hr = 0x%x", hr));
    }
    return hr;
}
