/* $Id: VBoxD3DAdapter9.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - Gallium driver interface.
 *
 * ID3DAdapter9 C wrappers.
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

#if 0
/*
 * Can't use Windows SDK headers because WINAPI is defined as empty in Mesa.
 *
 * Include mesa D3D9 headers in that order. This will prevent inclusion of d3d9.h
 * from d3dadapter9.h, because _D3D9_H_ and other guard defines will be already
 * defined by D3D9/d3d9.h, etc.
 */
#include <D3D9/d3d9types.h>
#include <D3D9/d3d9caps.h>
#include <D3D9/d3d9.h>
#ifndef _D3D9_H_
#error Gallium header did not define_D3D9_H_
#endif
#endif

#include <iprt/win/windows.h>
#include <d3d9types.h>
#include <d3d9caps.h>
#include <iprt/win/d3d9.h>
#include <d3dadapter/d3dadapter9.h>

/* ID3DAdapter9 interface has a C++ declaration. However Gallium uses C.
 * The wrappers makes sure that we are using "native" Gallium interface from WDDM C++ code.
 */

HRESULT D3DAdapter9_QueryInterface(ID3DAdapter9 *This, REFIID riid, void **ppvObject)
{
    return ID3DAdapter9_QueryInterface(This, riid, ppvObject);
}

ULONG D3DAdapter9_AddRef(ID3DAdapter9 *This)
{
    return ID3DAdapter9_AddRef(This);
}

ULONG D3DAdapter9_Release(ID3DAdapter9 *This)
{
    return ID3DAdapter9_Release(This);
}

HRESULT D3DAdapter9_GetAdapterIdentifier(ID3DAdapter9 *This, DWORD Flags, D3DADAPTER_IDENTIFIER9 *pIdentifier)
{
    return ID3DAdapter9_GetAdapterIdentifier(This, Flags, pIdentifier);
}

HRESULT D3DAdapter9_CheckDeviceType(ID3DAdapter9 *This, D3DDEVTYPE DevType, D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed)
{
    return ID3DAdapter9_CheckDeviceType(This, DevType, AdapterFormat, BackBufferFormat, bWindowed);
}

HRESULT D3DAdapter9_CheckDeviceFormat(ID3DAdapter9 *This, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat)
{
    return ID3DAdapter9_CheckDeviceFormat(This, DeviceType, AdapterFormat, Usage, RType, CheckFormat);
}

HRESULT D3DAdapter9_CheckDeviceMultiSampleType(ID3DAdapter9 *This, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD *pQualityLevels)
{
    return ID3DAdapter9_CheckDeviceMultiSampleType(This, DeviceType, SurfaceFormat, Windowed, MultiSampleType, pQualityLevels);
}

HRESULT D3DAdapter9_CheckDepthStencilMatch(ID3DAdapter9 *This, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat)
{
    return ID3DAdapter9_CheckDepthStencilMatch(This, DeviceType, AdapterFormat, RenderTargetFormat, DepthStencilFormat);
}

HRESULT D3DAdapter9_CheckDeviceFormatConversion(ID3DAdapter9 *This, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat)
{
    return ID3DAdapter9_CheckDeviceFormatConversion(This, DeviceType, SourceFormat, TargetFormat);
}

HRESULT D3DAdapter9_GetDeviceCaps(ID3DAdapter9 *This, D3DDEVTYPE DeviceType, D3DCAPS9 *pCaps)
{
    return ID3DAdapter9_GetDeviceCaps(This, DeviceType, pCaps);
}

HRESULT D3DAdapter9_CreateDevice(ID3DAdapter9 *This, UINT RealAdapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS *pPresentationParameters, IDirect3D9 *pD3D9, ID3DPresentGroup *pPresentationFactory, IDirect3DDevice9 **ppReturnedDeviceInterface)
{
    return ID3DAdapter9_CreateDevice(This, RealAdapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, pD3D9, pPresentationFactory, ppReturnedDeviceInterface);
}

HRESULT D3DAdapter9_CreateDeviceEx(ID3DAdapter9 *This, UINT RealAdapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS *pPresentationParameters, D3DDISPLAYMODEEX *pFullscreenDisplayMode, IDirect3D9Ex *pD3D9Ex, ID3DPresentGroup *pPresentationFactory, IDirect3DDevice9Ex **ppReturnedDeviceInterface)
{
    return ID3DAdapter9_CreateDeviceEx(This, RealAdapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, pFullscreenDisplayMode, pD3D9Ex, pPresentationFactory, ppReturnedDeviceInterface);
}
