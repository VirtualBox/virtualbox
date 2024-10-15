/* $Id: GaDrvTest.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Gallium backend test for for early development stages.
 * Use it only with kernel debugger attached to the VM.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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
#include "../VBoxDispD3D.h"

#include "test/d3d9render.h"

#include <iprt/asm.h>

void GaDrvTest(IGalliumStack *pGalliumStack, PVBOXWDDMDISP_DEVICE pDevice)
{
    ASMBreakpoint();

    DWORD fFlags =   D3DCREATE_HARDWARE_VERTEXPROCESSING
                   | D3DCREATE_FPU_PRESERVE;

    D3DPRESENT_PARAMETERS pp;
    RT_ZERO(pp);
    pp.BackBufferWidth            = 1024;
    pp.BackBufferHeight           = 768;
    pp.BackBufferFormat           = D3DFMT_X8R8G8B8;
    pp.BackBufferCount            = 1;
    pp.MultiSampleType            = D3DMULTISAMPLE_NONE;
    pp.MultiSampleQuality         = 0;
    pp.SwapEffect                 = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow              = 0;
    pp.Windowed                   = TRUE;
    pp.EnableAutoDepthStencil     = TRUE;
    pp.AutoDepthStencilFormat     = D3DFMT_D24S8;
    pp.Flags                      = 0;
    pp.FullScreen_RefreshRateInHz = 0;
    pp.PresentationInterval       = D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9Ex *pDevice9 = NULL;
    HRESULT hr = pGalliumStack->GaCreateDeviceEx(D3DDEVTYPE_HAL, NULL, fFlags, &pp, NULL,
                                                 pDevice->pAdapter->hAdapter, pDevice->hDevice, &pDevice->RtCallbacks,
                                                 &pDevice->pAdapter->AdapterInfo.u.vmsvga.HWInfo,
                                                 &pDevice9);
    GaAssertHR(hr);

    if (SUCCEEDED(hr))
    {
        D3D9Render *pRender = CreateRender(3);

        if (pRender)
        {
            hr = pRender->InitRender(pDevice9);
            GaAssertHR(hr);

            if (SUCCEEDED(hr))
            {
                /* Five times should be enough for a debugging session. */
                int i;
                for (i = 0; i < 5; ++i)
                {
                    ASMBreakpoint();

                    hr = pRender->DoRender(pDevice9);
                    GaAssertHR(hr);

                    ASMBreakpoint();
                }
            }

            DeleteRender(pRender);
        }
        else
        {
            AssertFailed();
        }

        pDevice9->Release();
    }

    ASMBreakpoint();
}
