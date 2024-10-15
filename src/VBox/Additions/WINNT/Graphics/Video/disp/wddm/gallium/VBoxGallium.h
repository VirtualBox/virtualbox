/* $Id: VBoxGallium.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - Gallium driver interface for WDDM user mode driver.
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_VBoxGallium_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_VBoxGallium_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/win/d3d9.h>

#include <VBoxGaDriver.h>

#include "GaDdi.h"

#include <iprt/cdefs.h>
#pragma warning(push)
#if RT_MSC_PREREQ(RT_MSC_VER_VC142)
# pragma warning(disable: 5204) /* warning C5204: 'IGaDirect3DDevice9Ex': class has virtual functions, but its trivial destructor is not virtual; instances of objects derived from this class may not be destructed correctly */
#endif

class IGalliumStack;
struct ID3DAdapter9;

DEFINE_GUID(IID_IGaDirect3DDevice9Ex, 0x0EF5C0C0, 0x392D, 0x4220, 0xBA, 0xB3, 0x8B, 0xB2, 0x10, 0x66, 0x14, 0xA4);
class IGaDirect3DDevice9Ex: public IDirect3DDevice9Ex
{
    public:
        STDMETHOD(EscapeCb)(THIS_ const void *pvData, uint32_t cbData, bool fHardwareAccess) PURE;
        STDMETHOD(GaSurfaceId)(THIS_ IUnknown *pSurface, uint32_t *pu32Sid) PURE;
        STDMETHOD(GaWDDMContextHandle)(THIS_ HANDLE *phContext) PURE;
        STDMETHOD(GaFlush)(THIS) PURE;
};

DEFINE_GUID(IID_IGaDirect3D9Ex, 0x20741f1d, 0x6525, 0x490A, 0x87, 0x40, 0x85, 0x4F, 0xFD, 0xD5, 0xCB, 0xB8);
class IGaDirect3D9Ex: public IDirect3D9Ex
{
    public:
        STDMETHOD_(IGalliumStack *, GetGalliumStack)(THIS) PURE;
        STDMETHOD_(ID3DAdapter9 *, GetAdapter9)(THIS) PURE;
#if VBOX_MESA_V_MAJOR < 24
        STDMETHOD_(struct pipe_screen *, GetScreen)(THIS) PURE;
#else
        STDMETHOD_(const WDDMGalliumDriverEnv *, GetWDDMEnv)(THIS) PURE;
#endif
};

/* Top interface to access Gallium API. */
class IGalliumStack: public IUnknown
{
    public:
        STDMETHOD(CreateDirect3DEx)(HANDLE hAdapter,
                                    HANDLE hDevice,
                                    const D3DDDI_DEVICECALLBACKS *pDeviceCallbacks,
                                    const VBOXGAHWINFO *pHWInfo,
                                    IDirect3D9Ex **ppOut) PURE;
        STDMETHOD(GaCreateDeviceEx)(THIS_
                                    D3DDEVTYPE DeviceType,HWND hFocusWindow,DWORD BehaviorFlags,
                                    D3DPRESENT_PARAMETERS* pPresentationParameters,
                                    D3DDISPLAYMODEEX* pFullscreenDisplayMode,
                                    HANDLE hAdapter,
                                    HANDLE hDevice,
                                    const D3DDDI_DEVICECALLBACKS *pDeviceCallbacks,
                                    const VBOXGAHWINFO *pHWInfo,
                                    IDirect3DDevice9Ex** ppReturnedDeviceInterface) PURE;

#if VBOX_MESA_V_MAJOR < 24
        STDMETHOD(GaNineD3DAdapter9Create)(struct pipe_screen *s, ID3DAdapter9 **ppOut) PURE;
        STDMETHOD_(struct pipe_resource *, GaNinePipeResourceFromSurface)(IUnknown *pSurface) PURE;
        STDMETHOD_(struct pipe_context *, GaNinePipeContextFromDevice)(IDirect3DDevice9 *pDevice) PURE;
#else
        STDMETHOD(GaNineD3DAdapter9Create)(const WDDMGalliumDriverEnv *pEnv, ID3DAdapter9 **ppOut) PURE;
        STDMETHOD_(uint32_t, GaNineGetSurfaceId)(IUnknown *pSurface) PURE;
        STDMETHOD_(uint32_t, GaNineGetContextId)(IDirect3DDevice9 *pDevice) PURE;
        STDMETHOD_(void, GaNineFlush)(IDirect3DDevice9 *pDevice) PURE;
#endif

#if VBOX_MESA_V_MAJOR < 24
        STDMETHOD_(struct pipe_screen *, GaDrvScreenCreate)(const WDDMGalliumDriverEnv *pEnv) PURE;
        STDMETHOD_(void, GaDrvScreenDestroy)(struct pipe_screen *s) PURE;
        STDMETHOD_(WDDMGalliumDriverEnv const *, GaDrvGetWDDMEnv)(struct pipe_screen *pScreen) PURE;
        STDMETHOD_(uint32_t, GaDrvGetContextId)(struct pipe_context *pPipeContext) PURE;
        STDMETHOD_(uint32_t, GaDrvGetSurfaceId)(struct pipe_screen *pScreen, struct pipe_resource *pResource) PURE;
        STDMETHOD_(void, GaDrvContextFlush)(struct pipe_context *pPipeContext) PURE;
#endif
};

#pragma warning(pop)


HRESULT GalliumStackCreate(IGalliumStack **ppOut);

/*
 * WDDM helpers.
 */
HRESULT GaD3DIfDeviceCreate(struct VBOXWDDMDISP_DEVICE *pDevice);
HRESULT GaD3DIfCreateForRc(struct VBOXWDDMDISP_RESOURCE *pRc);
IUnknown *GaD3DIfCreateSharedPrimary(struct VBOXWDDMDISP_ALLOCATION *pAlloc);
HRESULT GaD3DResourceSynchMem(struct VBOXWDDMDISP_RESOURCE *pRc, bool fToBackend);

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_VBoxGallium_h */
