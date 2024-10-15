/* $Id: Present.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - ID3DPresent and ID3DPresentGroup implementation.
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

#include "VBoxPresent.h"
#include "VBoxGallium.h"

#include <iprt/alloc.h>
#include <iprt/asm.h>
#include <iprt/log.h>

#include "../../../common/wddm/VBoxMPIf.h"

#define TRAPNOTIMPL do { \
    ASMBreakpoint(); \
} while (0)

const GUID IID_ID3DPresent = { 0x77D60E80, 0xF1E6, 0x11DF, { 0x9E, 0x39, 0x95, 0x0C, 0xDF, 0xD7, 0x20, 0x85 } };
const GUID IID_ID3DPresentGroup = { 0xB9C3016E, 0xF32A, 0x11DF, { 0x9C, 0x18, 0x92, 0xEA, 0xDE, 0xD7, 0x20, 0x85 } };

/*
 * Gallium D3D9 state tracker (nine) uses ID3DPresent interface to present (display) rendered buffers,
 * when IDirect3DDevice9::Present method is called.
 *
 * The WDDM driver uses this mechanism _only_ when running the embedded GaDrvTest code.
 *
 * I.e. during normal operations ID3DPresent interface is _not_ used.
 *
 * However 'nine' creates the buffers for the implicit swapchain.
 *
 * The implementation here simply stores the surface id and dimensions in D3DWindowBuffer
 * structure and tells the host to display the surface in PresentBuffer.
 *
 * Most of methods will trigger a breakpoint, because it its not expected that they willl be called.
 */

struct D3DWindowBuffer
{
    uint32_t u32Width;
    uint32_t u32Height;
    uint32_t u32Sid;
};

class WDDMPresentGroup;

class WDDMPresent: public ID3DPresent
{
    public:
        /* IUnknown */
        virtual HRESULT WINAPI QueryInterface(REFIID riid, void **ppvObject);
        virtual ULONG   WINAPI AddRef();
        virtual ULONG   WINAPI Release();

        /* ID3DPresent */
        virtual HRESULT WINAPI SetPresentParameters(D3DPRESENT_PARAMETERS *pPresentationParameters, D3DDISPLAYMODEEX *pFullscreenDisplayMode);
        virtual HRESULT WINAPI NewD3DWindowBufferFromDmaBuf(int dmaBufFd, int width, int height, int stride, int depth, int bpp, D3DWindowBuffer **out);
        virtual HRESULT WINAPI DestroyD3DWindowBuffer(D3DWindowBuffer *buffer);
        virtual HRESULT WINAPI WaitBufferReleased(D3DWindowBuffer *buffer);
        virtual HRESULT WINAPI FrontBufferCopy(D3DWindowBuffer *buffer);
        virtual HRESULT WINAPI PresentBuffer(D3DWindowBuffer *buffer, HWND hWndOverride, const RECT *pSourceRect, const RECT *pDestRect, const RGNDATA *pDirtyRegion, DWORD Flags);
        virtual HRESULT WINAPI GetRasterStatus(D3DRASTER_STATUS *pRasterStatus);
        virtual HRESULT WINAPI GetDisplayMode(D3DDISPLAYMODEEX *pMode, D3DDISPLAYROTATION *pRotation);
        virtual HRESULT WINAPI GetPresentStats(D3DPRESENTSTATS *pStats);
        virtual HRESULT WINAPI GetCursorPos(POINT *pPoint);
        virtual HRESULT WINAPI SetCursorPos(POINT *pPoint);
        virtual HRESULT WINAPI SetCursor(void *pBitmap, POINT *pHotspot, BOOL bShow);
        virtual HRESULT WINAPI SetGammaRamp(const D3DGAMMARAMP *pRamp, HWND hWndOverride);
        virtual HRESULT WINAPI GetWindowInfo(HWND hWnd, int *width, int *height, int *depth);

        WDDMPresent();
        virtual ~WDDMPresent();

        HRESULT Init(WDDMPresentGroup *pPresentGroup, D3DPRESENT_PARAMETERS *pPresentationParameters);

    private:
        volatile ULONG mcRefs;
        WDDMPresentGroup *mpPresentGroup;
};

class WDDMPresentGroup: public ID3DPresentGroup
{
    public:
        /* IUnknown */
        virtual HRESULT WINAPI QueryInterface(REFIID riid, void **ppvObject);
        virtual ULONG   WINAPI AddRef();
        virtual ULONG   WINAPI Release();

        /* ID3DPresentGroup */
        virtual UINT    WINAPI GetMultiheadCount();
        virtual HRESULT WINAPI GetPresent(UINT Index, ID3DPresent **ppPresent);
        virtual HRESULT WINAPI CreateAdditionalPresent(D3DPRESENT_PARAMETERS *pPresentationParameters, ID3DPresent **ppPresent);
        virtual void    WINAPI GetVersion(int *major, int *minor);

        WDDMPresentGroup();
        virtual ~WDDMPresentGroup();

        HRESULT Init(int cPresentBackends, IGaDirect3DDevice9Ex *pGaDevice);
        IGaDirect3DDevice9Ex *GaDevice() { return mpGaDevice; }

    private:
        volatile ULONG mcRefs;

        IGaDirect3DDevice9Ex *mpGaDevice; /** @todo ref count?  */
        int mcPresentBackends;
        WDDMPresent **mpaPresentBackends;
};


/*
 * WDDMPresent implementation.
 */

WDDMPresent::WDDMPresent()
    :
    mcRefs(0)
{
}

WDDMPresent::~WDDMPresent()
{
}

HRESULT WDDMPresent::Init(WDDMPresentGroup *pPresentGroup, D3DPRESENT_PARAMETERS *pPresentationParameters)
{
    RT_NOREF(pPresentationParameters);
    mpPresentGroup = pPresentGroup;
    return S_OK;
}

ULONG WINAPI WDDMPresent::AddRef()
{
    ULONG refs = InterlockedIncrement(&mcRefs);
    return refs;
}

ULONG WINAPI WDDMPresent::Release()
{
    ULONG refs = InterlockedDecrement(&mcRefs);
    if (refs == 0)
    {
        delete this;
    }
    return refs;
}

HRESULT WINAPI WDDMPresent::QueryInterface(REFIID riid,
                                           void **ppvObject)
{
    if (!ppvObject)
    {
        return E_POINTER;
    }

    if (   IsEqualGUID(IID_IUnknown, riid)
        || IsEqualGUID(IID_ID3DPresent, riid))
    {
        AddRef();
        *ppvObject = this;
        return S_OK;
    }

    *ppvObject = NULL;

    return E_NOINTERFACE;
}

HRESULT WINAPI WDDMPresent::SetPresentParameters(D3DPRESENT_PARAMETERS *pPresentationParameters, D3DDISPLAYMODEEX *pFullscreenDisplayMode)
{
    RT_NOREF(pPresentationParameters);
    RT_NOREF(pFullscreenDisplayMode);
    /* Ignore. */
    return S_OK;
}

HRESULT WINAPI WDDMPresent::NewD3DWindowBufferFromDmaBuf(int dmaBufFd, int width, int height, int stride, int depth, int bpp, D3DWindowBuffer **out)
{
    RT_NOREF3(stride, depth, bpp);

    D3DWindowBuffer *pBuffer = (D3DWindowBuffer *)RTMemAlloc(sizeof(D3DWindowBuffer));
    if (!pBuffer)
        return E_OUTOFMEMORY;

    pBuffer->u32Width = width;
    pBuffer->u32Height = height;
    pBuffer->u32Sid = dmaBufFd;

    *out = pBuffer;
    return S_OK;
}

HRESULT WINAPI WDDMPresent::DestroyD3DWindowBuffer(D3DWindowBuffer *buffer)
{
    RTMemFree(buffer);
    return S_OK;
}

HRESULT WINAPI WDDMPresent::WaitBufferReleased(D3DWindowBuffer *buffer)
{
    RT_NOREF(buffer);
    /* Ignore */
    return D3D_OK;
}

HRESULT WINAPI WDDMPresent::FrontBufferCopy(D3DWindowBuffer *buffer)
{
    RT_NOREF(buffer);
    TRAPNOTIMPL;
    return D3DERR_INVALIDCALL;
}

HRESULT WINAPI WDDMPresent::PresentBuffer(D3DWindowBuffer *pBuffer, HWND hWndOverride, const RECT *pSourceRect, const RECT *pDestRect, const RGNDATA *pDirtyRegion, DWORD Flags)
{
    RT_NOREF5(hWndOverride, pSourceRect, pDestRect, pDirtyRegion, Flags);

    ASMBreakpoint(); /* This is expected to run only as part of GaDrvTest under kernel debugger. */

    IGaDirect3DDevice9Ex *pGaDevice = mpPresentGroup->GaDevice();

    VBOXDISPIFESCAPE_GAPRESENT data;
    RT_ZERO(data);
    data.EscapeHdr.escapeCode = VBOXESC_GAPRESENT;
    data.u32Sid = pBuffer->u32Sid;
    data.u32Width = pBuffer->u32Width;
    data.u32Height = pBuffer->u32Height;
    HRESULT hr = pGaDevice->EscapeCb(&data, sizeof(data), /* fHardwareAccess= */ true);
    if (SUCCEEDED(hr))
        return D3D_OK;

    return D3DERR_INVALIDCALL;
}

HRESULT WINAPI WDDMPresent::GetRasterStatus(D3DRASTER_STATUS *pRasterStatus)
{
    RT_NOREF(pRasterStatus);
    TRAPNOTIMPL;
    return D3DERR_INVALIDCALL;
}

HRESULT WINAPI WDDMPresent::GetDisplayMode(D3DDISPLAYMODEEX *pMode, D3DDISPLAYROTATION *pRotation)
{
    RT_NOREF2(pMode, pRotation);
    TRAPNOTIMPL;
    return D3DERR_INVALIDCALL;
}

HRESULT WINAPI WDDMPresent::GetPresentStats(D3DPRESENTSTATS *pStats)
{
    RT_NOREF(pStats);
    TRAPNOTIMPL;
    return D3DERR_INVALIDCALL;
}

HRESULT WINAPI WDDMPresent::GetCursorPos(POINT *pPoint)
{
    if (!::GetCursorPos(pPoint))
    {
        pPoint->x = 0;
        pPoint->y = 0;
    }
    return S_OK;
}

HRESULT WINAPI WDDMPresent::SetCursorPos(POINT *pPoint)
{
    RT_NOREF(pPoint);
    TRAPNOTIMPL;
    return D3DERR_INVALIDCALL;
}

HRESULT WINAPI WDDMPresent::SetCursor(void *pBitmap, POINT *pHotspot, BOOL bShow)
{
    RT_NOREF3(pBitmap, pHotspot, bShow);
    TRAPNOTIMPL;
    return D3DERR_INVALIDCALL;
}

HRESULT WINAPI WDDMPresent::SetGammaRamp(const D3DGAMMARAMP *pRamp, HWND hWndOverride)
{
    RT_NOREF2(pRamp, hWndOverride);
    TRAPNOTIMPL;
    return D3DERR_INVALIDCALL;
}

HRESULT WINAPI WDDMPresent::GetWindowInfo(HWND hWnd, int *width, int *height, int *depth)
{
    RT_NOREF4(hWnd, width, height, depth);
    TRAPNOTIMPL;
    return D3DERR_INVALIDCALL;
}


/*
 * WDDMPresentGroup implementation.
 */
WDDMPresentGroup::WDDMPresentGroup()
    :
    mcRefs(0),
    mcPresentBackends(0),
    mpaPresentBackends(NULL)
{
}

WDDMPresentGroup::~WDDMPresentGroup()
{
    if (mpaPresentBackends)
    {
        int i;
        for (i = 0; i < mcPresentBackends; ++i)
        {
            if (mpaPresentBackends[i])
            {
                mpaPresentBackends[i]->Release();
                mpaPresentBackends[i] = NULL;
            }
        }

        RTMemFree(mpaPresentBackends);
        mpaPresentBackends = NULL;
    }
}

HRESULT WDDMPresentGroup::Init(int cPresentBackends, IGaDirect3DDevice9Ex *pGaDevice)
{
    mpGaDevice = pGaDevice;
    mcPresentBackends = cPresentBackends;
    mpaPresentBackends = (WDDMPresent **)RTMemAllocZ(mcPresentBackends);
    if (!mpaPresentBackends)
    {
        return E_OUTOFMEMORY;
    }

    int i;
    for (i = 0; i < mcPresentBackends; ++i)
    {
        WDDMPresent *p = new WDDMPresent();
        if (!p)
        {
            return E_OUTOFMEMORY;
        }

        HRESULT hr = p->Init(this, NULL);
        if (FAILED(hr))
        {
            delete p;
            return hr;
        }

        p->AddRef();
        mpaPresentBackends[i] = p;
    }

    return S_OK;
}

ULONG WINAPI WDDMPresentGroup::AddRef()
{
    ULONG refs = InterlockedIncrement(&mcRefs);
    return refs;
}

ULONG WINAPI WDDMPresentGroup::Release()
{
    ULONG refs = InterlockedDecrement(&mcRefs);
    if (refs == 0)
    {
        delete this;
    }
    return refs;
}

HRESULT WINAPI WDDMPresentGroup::QueryInterface(REFIID riid,
                                         void **ppvObject)
{
    if (!ppvObject)
    {
        return E_POINTER;
    }

    if (   IsEqualGUID(IID_IUnknown, riid)
        || IsEqualGUID(IID_ID3DPresentGroup, riid))
    {
        AddRef();
        *ppvObject = this;
        return S_OK;
    }

    *ppvObject = NULL;

    return E_NOINTERFACE;
}

UINT WINAPI WDDMPresentGroup::GetMultiheadCount()
{
    return mcPresentBackends;
}

HRESULT WINAPI WDDMPresentGroup::GetPresent(UINT Index, ID3DPresent **ppPresent)
{
    if (Index >= GetMultiheadCount())
    {
        return D3DERR_INVALIDCALL;
    }

    mpaPresentBackends[Index]->AddRef();
    *ppPresent = mpaPresentBackends[Index];

    return S_OK;
}

HRESULT WINAPI WDDMPresentGroup::CreateAdditionalPresent(D3DPRESENT_PARAMETERS *pPresentationParameters,
                                                         ID3DPresent **ppPresent)
{
    WDDMPresent *p = new WDDMPresent();
    if (!p)
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = p->Init(this, pPresentationParameters);
    if (SUCCEEDED(hr))
    {
        p->AddRef();
        *ppPresent = p;
    }
    else
    {
        delete p;
    }

    return hr;
}

void WINAPI WDDMPresentGroup::GetVersion(int *major, int *minor)
{
    *major = 1;
    *minor = 0;
}

HRESULT WDDMPresentGroupCreate(IGaDirect3DDevice9Ex *pGaDevice, ID3DPresentGroup **ppOut)
{
    WDDMPresentGroup *p = new WDDMPresentGroup();
    if (!p)
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = p->Init(1, pGaDevice);
    if (SUCCEEDED(hr))
    {
        p->AddRef();
        *ppOut = p;
    }
    else
    {
        delete p;
    }

    return hr;
}
