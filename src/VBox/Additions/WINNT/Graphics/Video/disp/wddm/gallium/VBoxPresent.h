/* $Id: VBoxPresent.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - Gallium D3D9 state tracker interface.
 *
 * ID3DPresent and ID3DPresentGroup declarations.
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_VBoxPresent_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_VBoxPresent_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/win/d3d9.h>

typedef struct D3DWindowBuffer D3DWindowBuffer;

#ifdef __cplusplus

# include <iprt/cdefs.h>
# pragma warning(push)
# if RT_MSC_PREREQ(RT_MSC_VER_VC142)
#  pragma warning(disable: 5204) /* warning C5204: 'ID3DPresent': class has virtual functions, but its trivial destructor is not virtual; instances of objects derived from this class may not be destructed correctly */
# endif

/*
 * Gallium only defines C interface for ID3DPresentGroup and ID3DPresent.
 *
 * Make sure that WINAPI is __stdcall in Gallium include/D3D9/d3d9types.h for the Windows build.
 */
class ID3DPresent
{
    public:
        /* IUnknown */
        virtual HRESULT WINAPI QueryInterface(REFIID riid, void **ppvObject) = 0;
        virtual ULONG   WINAPI AddRef() = 0;
        virtual ULONG   WINAPI Release() = 0;

        /* ID3DPresent */
        virtual HRESULT WINAPI SetPresentParameters(D3DPRESENT_PARAMETERS *pPresentationParameters, D3DDISPLAYMODEEX *pFullscreenDisplayMode) = 0;
        virtual HRESULT WINAPI NewD3DWindowBufferFromDmaBuf(int dmaBufFd, int width, int height, int stride, int depth, int bpp, D3DWindowBuffer **out) = 0;
        virtual HRESULT WINAPI DestroyD3DWindowBuffer(D3DWindowBuffer *buffer) = 0;
        virtual HRESULT WINAPI WaitBufferReleased(D3DWindowBuffer *buffer) = 0;
        virtual HRESULT WINAPI FrontBufferCopy(D3DWindowBuffer *buffer) = 0;
        virtual HRESULT WINAPI PresentBuffer(D3DWindowBuffer *buffer, HWND hWndOverride, const RECT *pSourceRect, const RECT *pDestRect, const RGNDATA *pDirtyRegion, DWORD Flags) = 0;
        virtual HRESULT WINAPI GetRasterStatus(D3DRASTER_STATUS *pRasterStatus) = 0;
        virtual HRESULT WINAPI GetDisplayMode(D3DDISPLAYMODEEX *pMode, D3DDISPLAYROTATION *pRotation) = 0;
        virtual HRESULT WINAPI GetPresentStats(D3DPRESENTSTATS *pStats) = 0;
        virtual HRESULT WINAPI GetCursorPos(POINT *pPoint) = 0;
        virtual HRESULT WINAPI SetCursorPos(POINT *pPoint) = 0;
        virtual HRESULT WINAPI SetCursor(void *pBitmap, POINT *pHotspot, BOOL bShow) = 0;
        virtual HRESULT WINAPI SetGammaRamp(const D3DGAMMARAMP *pRamp, HWND hWndOverride) = 0;
        virtual HRESULT WINAPI GetWindowInfo(HWND hWnd, int *width, int *height, int *depth) = 0;
};

class ID3DPresentGroup
{
    public:
        /* IUnknown */
        virtual HRESULT WINAPI QueryInterface(REFIID riid, void **ppvObject) = 0;
        virtual ULONG   WINAPI AddRef() = 0;
        virtual ULONG   WINAPI Release() = 0;

        /* ID3DPresentGroup */
        virtual UINT    WINAPI GetMultiheadCount() = 0;
        virtual HRESULT WINAPI GetPresent(UINT Index, ID3DPresent **ppPresent) = 0;
        virtual HRESULT WINAPI CreateAdditionalPresent(D3DPRESENT_PARAMETERS *pPresentationParameters, ID3DPresent **ppPresent) = 0;
        virtual void    WINAPI GetVersion(int *major, int *minor) = 0;
};

# pragma warning(pop)

class IGaDirect3DDevice9Ex;
HRESULT WDDMPresentGroupCreate(IGaDirect3DDevice9Ex *pGaDevice, ID3DPresentGroup **ppOut);

#else /* !__cplusplus */
typedef struct ID3DPresent ID3DPresent;
typedef struct ID3DPresentGroup ID3DPresentGroup;
#endif

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_VBoxPresent_h */
