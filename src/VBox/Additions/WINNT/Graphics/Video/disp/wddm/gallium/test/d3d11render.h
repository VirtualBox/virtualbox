/* $Id: d3d11render.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Gallium D3D testcase. Interface for D3D11 tests.
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_test_d3d11render_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_test_d3d11render_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <d3d11_1.h>

#include <iprt/asm.h>
#include <iprt/cdefs.h>
#include <iprt/string.h>

#define D3D_RELEASE(ptr) do { \
    if (ptr)                  \
    {                         \
        (ptr)->Release();     \
        (ptr) = 0;            \
    }                         \
} while (0)

#define D3D_RELEASE_ARRAY(a_Count, a_papArray) do { \
    for (uint32_t i = 0; i < (a_Count); ++i) \
        D3D_RELEASE((a_papArray)[i]); \
} while (0)

inline void D3DTestShowError(HRESULT hr, const char *pszString)
{
    (void)hr;
    MessageBoxA(0, pszString, 0, 0);
}

/* Expand __LINE__ number to string. */
#define D3DTEST_S(n) #n
#define D3DTEST_N2S(n) D3DTEST_S(n)

#define D3DAssertHR(_hr) do { if (FAILED(_hr)) D3DTestShowError((_hr), __FILE__ "@" D3DTEST_N2S(__LINE__)); } while(0)

#define HTEST(a) do { \
    hr = a; \
    D3DAssertHR(hr); \
} while (0)

class D3D11DeviceProvider
{
public:
    virtual ~D3D11DeviceProvider() {}
    virtual ID3D11Device *Device() = 0;
    virtual ID3D11DeviceContext *ImmediateContext() = 0;
    virtual ID3D11RenderTargetView *RenderTargetView() = 0;
    virtual ID3D11DepthStencilView *DepthStencilView() = 0;
};

class D3D11Render
{
public:
    D3D11Render() {}
    virtual ~D3D11Render() {}
    virtual HRESULT InitRender(D3D11DeviceProvider *pDP) = 0;
    virtual HRESULT DoRender(D3D11DeviceProvider *pDP) = 0;
    virtual void TimeAdvance(float dt) { (void)dt; return; }
    virtual bool IsDepthStencilBufferRequired(D3D11DeviceProvider *pDP) { (void)pDP; return true; }
    virtual bool IsDirectOutputRequired(D3D11DeviceProvider *pDP) { (void)pDP; return false; }
};

D3D11Render *CreateRender(int iRenderId);
void DeleteRender(D3D11Render *pRender);

uint8_t *readBmpFile(char const *pszFilename, uint32_t cbPixel, uint32_t *pWidth, uint32_t *pHeight, uint32_t *pcbData);

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_test_d3d11render_h */
