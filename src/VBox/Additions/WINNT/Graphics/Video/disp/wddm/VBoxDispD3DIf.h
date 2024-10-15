/* $Id: VBoxDispD3DIf.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_VBoxDispD3DIf_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_VBoxDispD3DIf_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifdef VBOX_WITH_MESA3D
#include "gallium/VBoxGallium.h"
#endif

/* D3D headers */
#include <iprt/critsect.h>
#include <iprt/semaphore.h>
#include <iprt/win/d3d9.h>
#include <d3dumddi.h>
#include "../../common/wddm/VBoxMPIf.h"

typedef struct VBOXWDDMDISP_FORMATS
{
    uint32_t cFormatOps;
    const struct _FORMATOP* paFormatOps;
    uint32_t cSurfDescs;
    struct _DDSURFACEDESC *paSurfDescs;
} VBOXWDDMDISP_FORMATS, *PVBOXWDDMDISP_FORMATS;

typedef struct VBOXWDDMDISP_D3D *PVBOXWDDMDISP_D3D;
typedef void FNVBOXDISPD3DBACKENDCLOSE(PVBOXWDDMDISP_D3D pD3D);
typedef FNVBOXDISPD3DBACKENDCLOSE *PFNVBOXDISPD3DBACKENDCLOSE;

typedef struct VBOXWDDMDISP_D3D
{
    PFNVBOXDISPD3DBACKENDCLOSE pfnD3DBackendClose;

    D3DCAPS9 Caps;
    UINT cMaxSimRTs;

#ifdef VBOX_WITH_MESA3D
    /* Gallium backend. */
    IGalliumStack *pGalliumStack;
#endif
} VBOXWDDMDISP_D3D;

void VBoxDispD3DGlobalInit(void);
void VBoxDispD3DGlobalTerm(void);
HRESULT VBoxDispD3DGlobalOpen(PVBOXWDDMDISP_D3D pD3D, PVBOXWDDMDISP_FORMATS pFormats, VBOXWDDM_QAI const *pAdapterInfo);
void VBoxDispD3DGlobalClose(PVBOXWDDMDISP_D3D pD3D, PVBOXWDDMDISP_FORMATS pFormats);

#ifdef VBOX_WITH_VIDEOHWACCEL
HRESULT VBoxDispD3DGlobal2DFormatsInit(struct VBOXWDDMDISP_ADAPTER *pAdapter);
void VBoxDispD3DGlobal2DFormatsTerm(struct VBOXWDDMDISP_ADAPTER *pAdapter);
#endif

#ifdef DEBUG
void vboxDispCheckCapsLevel(const D3DCAPS9 *pCaps);
#endif

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_VBoxDispD3DIf_h */
