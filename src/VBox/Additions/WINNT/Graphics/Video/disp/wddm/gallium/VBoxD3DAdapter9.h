/* $Id: VBoxD3DAdapter9.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_VBoxD3DAdapter9_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_VBoxD3DAdapter9_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBoxPresent.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ID3DAdapter9 ID3DAdapter9;

HRESULT D3DAdapter9_QueryInterface(ID3DAdapter9 *This, REFIID riid, void **ppvObject);
ULONG D3DAdapter9_AddRef(ID3DAdapter9 *This);;
ULONG D3DAdapter9_Release(ID3DAdapter9 *This);
HRESULT D3DAdapter9_GetAdapterIdentifier(ID3DAdapter9 *This, DWORD Flags, D3DADAPTER_IDENTIFIER9 *pIdentifier);
HRESULT D3DAdapter9_CheckDeviceType(ID3DAdapter9 *This, D3DDEVTYPE DevType, D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed);
HRESULT D3DAdapter9_CheckDeviceFormat(ID3DAdapter9 *This, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat);
HRESULT D3DAdapter9_CheckDeviceMultiSampleType(ID3DAdapter9 *This, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD *pQualityLevels);
HRESULT D3DAdapter9_CheckDepthStencilMatch(ID3DAdapter9 *This, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat);
HRESULT D3DAdapter9_CheckDeviceFormatConversion(ID3DAdapter9 *This, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat);
HRESULT D3DAdapter9_GetDeviceCaps(ID3DAdapter9 *This, D3DDEVTYPE DeviceType, D3DCAPS9 *pCaps);
HRESULT D3DAdapter9_CreateDevice(ID3DAdapter9 *This, UINT RealAdapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS *pPresentationParameters, IDirect3D9 *pD3D9, ID3DPresentGroup *pPresentationFactory, IDirect3DDevice9 **ppReturnedDeviceInterface);
HRESULT D3DAdapter9_CreateDeviceEx(ID3DAdapter9 *This, UINT RealAdapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS *pPresentationParameters, D3DDISPLAYMODEEX *pFullscreenDisplayMode, IDirect3D9Ex *pD3D9Ex, ID3DPresentGroup *pPresentationFactory, IDirect3DDevice9Ex **ppReturnedDeviceInterface);

#ifdef __cplusplus
}
#endif

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_VBoxD3DAdapter9_h */
