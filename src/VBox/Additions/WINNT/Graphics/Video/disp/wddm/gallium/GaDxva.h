/* $Id: GaDxva.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox WDDM DXVA for the Gallium based driver.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_GaDxva_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_GaDxva_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <d3dumddi.h>

typedef struct VBOXWDDMDISP_DEVICE *PVBOXWDDMDISP_DEVICE;

HRESULT VBoxDxvaGetDeviceGuidCount(UINT *pcGuids);
HRESULT VBoxDxvaGetDeviceGuids(GUID *paGuids, UINT cbGuids);

HRESULT VBoxDxvaGetOutputFormatCount(UINT *pcFormats, DXVADDI_VIDEOPROCESSORINPUT const *pVPI, bool fSubstream);
HRESULT VBoxDxvaGetOutputFormats(D3DDDIFORMAT *paFormats, UINT cbFormats, DXVADDI_VIDEOPROCESSORINPUT const *pVPI, bool fSubstream);

HRESULT VBoxDxvaGetCaps(DXVADDI_VIDEOPROCESSORCAPS *pVideoProcessorCaps, DXVADDI_VIDEOPROCESSORINPUT const *pVPI);

HRESULT VBoxDxvaCreateVideoProcessDevice(PVBOXWDDMDISP_DEVICE pDevice, D3DDDIARG_CREATEVIDEOPROCESSDEVICE *pData);
HRESULT VBoxDxvaDestroyVideoProcessDevice(PVBOXWDDMDISP_DEVICE pDevice, HANDLE hVideoProcessor);
HRESULT VBoxDxvaVideoProcessBeginFrame(PVBOXWDDMDISP_DEVICE pDevice, HANDLE hVideoProcessor);
HRESULT VBoxDxvaVideoProcessEndFrame(PVBOXWDDMDISP_DEVICE pDevice, D3DDDIARG_VIDEOPROCESSENDFRAME *pData);
HRESULT VBoxDxvaSetVideoProcessRenderTarget(PVBOXWDDMDISP_DEVICE pDevice, const D3DDDIARG_SETVIDEOPROCESSRENDERTARGET *pData);
HRESULT VBoxDxvaVideoProcessBlt(PVBOXWDDMDISP_DEVICE pDevice, const D3DDDIARG_VIDEOPROCESSBLT *pData);

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_GaDxva_h */
