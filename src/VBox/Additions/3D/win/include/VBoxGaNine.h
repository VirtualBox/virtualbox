/* $Id: VBoxGaNine.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef GA_INCLUDED_3D_WIN_VBoxGaNine_h
#define GA_INCLUDED_3D_WIN_VBoxGaNine_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/win/d3d9.h>

#ifdef __cplusplus
extern "C" {
#endif

#if VBOX_MESA_V_MAJOR < 24
struct pipe_screen;
struct pipe_resource;
struct pipe_context;
#endif
typedef struct ID3DAdapter9 ID3DAdapter9;

#if VBOX_MESA_V_MAJOR < 24
typedef HRESULT WINAPI FNGaNineD3DAdapter9Create(struct pipe_screen *s, ID3DAdapter9 **ppOut);
typedef FNGaNineD3DAdapter9Create *PFNGaNineD3DAdapter9Create;

typedef struct pipe_resource * WINAPI FNGaNinePipeResourceFromSurface(IUnknown *pSurface);
typedef FNGaNinePipeResourceFromSurface *PFNGaNinePipeResourceFromSurface;

typedef struct pipe_context * WINAPI FNGaNinePipeContextFromDevice(IDirect3DDevice9 *pDevice);
typedef FNGaNinePipeContextFromDevice *PFNGaNinePipeContextFromDevice;
#else
typedef HRESULT WINAPI FNGaNineD3DAdapter9Create(const WDDMGalliumDriverEnv *pEnv, ID3DAdapter9 **ppOut);
typedef FNGaNineD3DAdapter9Create *PFNGaNineD3DAdapter9Create;

typedef uint32_t WINAPI FNGaNineGetSurfaceId(IUnknown *pSurface);
typedef FNGaNineGetSurfaceId *PFNGaNineGetSurfaceId;

typedef uint32_t WINAPI FNGaNineGetContextId(IDirect3DDevice9 *pDevice);
typedef FNGaNineGetContextId *PFNGaNineGetContextId;

typedef void WINAPI FNGaNineFlush(IDirect3DDevice9 *pDevice);
typedef FNGaNineFlush *PFNGaNineFlush;
#endif

#ifdef __cplusplus
}
#endif

#endif /* !GA_INCLUDED_3D_WIN_VBoxGaNine_h */
