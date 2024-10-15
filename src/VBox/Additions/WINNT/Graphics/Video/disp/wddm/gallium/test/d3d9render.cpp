/* $Id: d3d9render.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Gallium D3D testcase. Simple D3D9 tests.
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

#include "d3d9render.h"

#include <intrin.h>

static HRESULT d3dCopyToVertexBuffer(IDirect3DVertexBuffer9 *pVB, const void *pvSrc, int cbSrc)
{
    HRESULT hr = D3D_OK;
    void *pvDst = 0;
    HTEST(pVB->Lock(0, 0, &pvDst, 0));
    if (SUCCEEDED(hr))
    {
        memcpy(pvDst, pvSrc, cbSrc);
        HTEST(pVB->Unlock());
    }
    return hr;
}

static HRESULT d3dCopyToIndexBuffer(IDirect3DIndexBuffer9 *pIB, const void *pvSrc, int cbSrc)
{
    HRESULT hr = D3D_OK;
    void *pvDst = 0;
    HTEST(pIB->Lock(0, 0, &pvDst, 0));
    if (SUCCEEDED(hr))
    {
        memcpy(pvDst, pvSrc, cbSrc);
        HTEST(pIB->Unlock());
    }
    return hr;
}

static void drawTexture(IDirect3DDevice9 *pDevice, IDirect3DTexture9 *pTexture, int x, int y, int w, int h)
{
    HRESULT hr = S_OK;

    HTEST(pDevice->Clear(0, 0, D3DCLEAR_TARGET, 0xffafaf00, 0.0f, 0));

    HTEST(pDevice->BeginScene());

    IDirect3DSurface9 *pSurface;
    HTEST(pTexture->GetSurfaceLevel(0, &pSurface));

    /* Copy the texture to the backbuffer. */
    IDirect3DSurface9 *pBackBuffer;
    HTEST(pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer));

    RECT rDst;
    rDst.left = x;
    rDst.top = y;
    rDst.right = x + w;
    rDst.bottom = y + h;

    HTEST(pDevice->StretchRect(pSurface, NULL, pBackBuffer, &rDst, D3DTEXF_POINT));

    HTEST(pDevice->EndScene());

    D3D_RELEASE(pBackBuffer);
    D3D_RELEASE(pSurface);
}


/*
 * Clear the backbuffer and display it.
 */

class D3D9RenderClear: public D3D9Render
{
public:
    D3D9RenderClear() {}
    virtual ~D3D9RenderClear() {}
    virtual HRESULT InitRender(D3D9DeviceProvider *pDP);
    virtual HRESULT DoRender(D3D9DeviceProvider *pDP);
};

HRESULT D3D9RenderClear::InitRender(D3D9DeviceProvider *pDP)
{
    (void)pDP;
    return S_OK;
}

HRESULT D3D9RenderClear::DoRender(D3D9DeviceProvider *pDP)
{
    IDirect3DDevice9 *pDevice = pDP->Device(0);

    pDevice->Clear(0, 0, D3DCLEAR_TARGET /* | D3DCLEAR_ZBUFFER */, 0xff0000ff, 1.0f, 0);

    /* Separately test depth. This triggered a unimplemented code path in SVGA driver. */
    D3DRECT r;
    r.x1 = 20;
    r.y1 = 20;
    r.x2 = 120;
    r.y2 = 120;
    pDevice->Clear(1, &r, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);

    pDevice->Present(0, 0, 0, 0);
    return S_OK;
}


/*
 * Minimal code to draw a black triangle.
 */

class D3D9RenderTriangle: public D3D9Render
{
public:
    D3D9RenderTriangle();
    virtual ~D3D9RenderTriangle();
    virtual HRESULT InitRender(D3D9DeviceProvider *pDP);
    virtual HRESULT DoRender(D3D9DeviceProvider *pDP);
private:
    IDirect3DVertexDeclaration9 *mpVertexDecl;
    IDirect3DVertexBuffer9 *mpVB;

    struct Vertex
    {
        D3DVECTOR position;
    };

    static D3DVERTEXELEMENT9 maVertexElements[];
};

D3DVERTEXELEMENT9 D3D9RenderTriangle::maVertexElements[] =
{
    { 0, GA_W_OFFSET_OF(Vertex, position), D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
    D3DDECL_END()
};

D3D9RenderTriangle::D3D9RenderTriangle()
    :
    mpVertexDecl(0),
    mpVB(0)
{
}

D3D9RenderTriangle::~D3D9RenderTriangle()
{
    D3D_RELEASE(mpVertexDecl);
    D3D_RELEASE(mpVB);
}

HRESULT D3D9RenderTriangle::InitRender(D3D9DeviceProvider *pDP)
{
    IDirect3DDevice9 *pDevice = pDP->Device(0);

    HRESULT hr = pDevice->CreateVertexDeclaration(maVertexElements, &mpVertexDecl);
    if (FAILED(hr))
    {
        return hr;
    }

    /* Coords are choosen to avoid setting the view and projection matrices. */
    static Vertex aVertices[] =
    {
        { {-0.5f, -0.5f, 0.9f } },
        { { 0.0f,  0.5f, 0.9f } },
        { { 0.5f, -0.5f, 0.9f } },
    };

    hr = pDevice->CreateVertexBuffer(sizeof(aVertices), 0, 0, D3DPOOL_DEFAULT, &mpVB, 0);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = d3dCopyToVertexBuffer(mpVB, aVertices, sizeof(aVertices));

    return hr;
}

HRESULT D3D9RenderTriangle::DoRender(D3D9DeviceProvider *pDP)
{
    IDirect3DDevice9 *pDevice = pDP->Device(0);

    HRESULT hr = S_OK;

    HTEST(pDevice->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff0000ff, 1.0f, 0));

    HTEST(pDevice->BeginScene());

    HTEST(pDevice->SetStreamSource(0, mpVB, 0, sizeof(Vertex)));
    HTEST(pDevice->SetVertexDeclaration(mpVertexDecl));

    HTEST(pDevice->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1));

    HTEST(pDevice->EndScene());

    HTEST(pDevice->Present(0, 0, 0, 0));
    return S_OK;
}


/*
 * Colorful triangle using FVF.
 */

class D3D9RenderTriangleFVF: public D3D9Render
{
public:
    D3D9RenderTriangleFVF();
    virtual ~D3D9RenderTriangleFVF();
    virtual HRESULT InitRender(D3D9DeviceProvider *pDP);
    virtual HRESULT DoRender(D3D9DeviceProvider *pDP);
private:
    IDirect3DVertexBuffer9 *mpVB;

    struct Vertex_XYZRHW_DIFFUSE
    {
        FLOAT x, y, z, rhw;
        DWORD color;
    };
};

#define FVF_XYZRHW_DIFFUSE (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

D3D9RenderTriangleFVF::D3D9RenderTriangleFVF()
    :
    mpVB(0)
{
}

D3D9RenderTriangleFVF::~D3D9RenderTriangleFVF()
{
    D3D_RELEASE(mpVB);
}

HRESULT D3D9RenderTriangleFVF::InitRender(D3D9DeviceProvider *pDP)
{
    IDirect3DDevice9 *pDevice = pDP->Device(0);

    static Vertex_XYZRHW_DIFFUSE aVertices[] =
    {
        {  50.0f,  50.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(  0,   0, 255), },
        { 150.0f,  50.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(  0, 255,   0), },
        { 100.0f, 150.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(255,   0,   0), },
    };

    HRESULT hr = S_OK;

    HTEST(pDevice->CreateVertexBuffer(sizeof(aVertices),
                                             0,
                                             FVF_XYZRHW_DIFFUSE,
                                             D3DPOOL_DEFAULT,
                                             &mpVB,
                                             0));
    if (FAILED(hr))
    {
        return hr;
    }

    HTEST(d3dCopyToVertexBuffer(mpVB, aVertices, sizeof(aVertices)));

    return hr;
}

HRESULT D3D9RenderTriangleFVF::DoRender(D3D9DeviceProvider *pDP)
{
    IDirect3DDevice9 *pDevice = pDP->Device(0);

    HRESULT hr = S_OK;

    HTEST(pDevice->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xffafafaf, 1.0f, 0));

    HTEST(pDevice->BeginScene());

    HTEST(pDevice->SetStreamSource(0, mpVB, 0, sizeof(Vertex_XYZRHW_DIFFUSE)));
    HTEST(pDevice->SetFVF(FVF_XYZRHW_DIFFUSE));

    HTEST(pDevice->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1));

    HTEST(pDevice->EndScene());

    HTEST(pDevice->Present(0, 0, 0, 0));
    return S_OK;
}


/*
 * Colorful triangle using shaders.
 */

class D3D9RenderTriangleShader: public D3D9Render
{
public:
    D3D9RenderTriangleShader();
    virtual ~D3D9RenderTriangleShader();
    virtual HRESULT InitRender(D3D9DeviceProvider *pDP);
    virtual HRESULT DoRender(D3D9DeviceProvider *pDP);
private:
    IDirect3DVertexBuffer9      *mpVB;
    IDirect3DVertexDeclaration9 *mpVertexDecl;
    IDirect3DVertexShader9      *mpVS;
    IDirect3DPixelShader9       *mpPS;

    struct Vertex
    {
        D3DVECTOR position;
        D3DCOLOR  color;
    };
    static D3DVERTEXELEMENT9 VertexElements[];
};

D3DVERTEXELEMENT9 D3D9RenderTriangleShader::VertexElements[] =
{
    {0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0},
    D3DDECL_END()
};


D3D9RenderTriangleShader::D3D9RenderTriangleShader()
    :
    mpVB(0),
    mpVertexDecl(0),
    mpVS(0),
    mpPS(0)
{
}

D3D9RenderTriangleShader::~D3D9RenderTriangleShader()
{
    D3D_RELEASE(mpVS);
    D3D_RELEASE(mpPS);
    D3D_RELEASE(mpVB);
    D3D_RELEASE(mpVertexDecl);
}

HRESULT D3D9RenderTriangleShader::InitRender(D3D9DeviceProvider *pDP)
{
    IDirect3DDevice9 *pDevice = pDP->Device(0);

    static DWORD aVSCode[] =
    {
        0xFFFE0200,                         // vs_2_0
        0x05000051, 0xa00f0000, 0x3f800000, 0x00000000, 0x00000000, 0x00000000, // def c0, 1, 0, 0, 0
        0x0200001f, 0x80000000, 0x900f0000, // dcl_position v0
        0x0200001f, 0x8000000a, 0x900f0001, // dcl_color v1
        0x02000001, 0xc0070000, 0x90e40000, // mov oPos.xyz, v0
        0x02000001, 0xc0080000, 0xa0000000, // mov oPos.w, c0.x
        0x02000001, 0xd00f0000, 0x90e40001, // mov oD0, v1
        0x0000FFFF
    };

    static DWORD aPSCode[] =
    {
        0xFFFF0200,                         // ps_2_0
        0x0200001f, 0x80000000, 0x900f0000, // dcl v0
        0x02000001, 0x800f0800, 0x90e40000, // mov oC0, v0
        0x0000FFFF
    };

    static DWORD aPSCode1[] =
    {
        0xFFFF0200,                         // ps_2_0
        0x05000051, 0xa00f0000, 0x3f800000, 0x00000000, 0x00000000, 0x00000000, // def c0, 1, 0, 0, 0
        0x02000001, 0x800f0000, 0xa0000000, // mov r0, c0.x
        0x02000001, 0x800f0800, 0x80e40000, // mov oC0, r0
        0x0000FFFF
    };

    static DWORD aPSCodeParallax[] =
    {
        0xFFFF0300,
        0x0200001F, 0x80010005, 0x900F0000,
        0x0200001F, 0x80020005, 0x900F0001,
        0x0200001F, 0x80030005, 0x900F0002,
        0x0200001F, 0x80040005, 0x900F0003,
        0x0200001F, 0x80050005, 0x900F0004,
        0x0200001F, 0x80060005, 0x900F0005,
        0x05000051, 0xA00F00F1, 0x3F6147AE, 0x3F451EB8, 0xBF6147AE, 0xBF451EB8,
        0x0000FFFF
    };

    static Vertex aVertices[] =
    {
        { { -0.5f, -0.5f, 0.5f }, D3DCOLOR_XRGB(  0,   0, 255), },
        { {  0.5f, -0.5f, 0.5f }, D3DCOLOR_XRGB(  0, 255,   0), },
        { {  0.0f,  0.5f, 0.5f }, D3DCOLOR_XRGB(255,   0,   0), },
    };

    HRESULT hr = S_OK;

    HTEST(pDevice->CreateVertexDeclaration(VertexElements, &mpVertexDecl));
    HTEST(pDevice->CreateVertexBuffer(sizeof(aVertices),
                                      0, /* D3DUSAGE_* */
                                      0, /* FVF */
                                      D3DPOOL_DEFAULT,
                                      &mpVB,
                                      0));
    HTEST(pDevice->CreateVertexShader(aVSCode, &mpVS));
    HTEST(pDevice->CreatePixelShader(aPSCode, &mpPS));

    HTEST(d3dCopyToVertexBuffer(mpVB, aVertices, sizeof(aVertices)));

    return hr;
}

HRESULT D3D9RenderTriangleShader::DoRender(D3D9DeviceProvider *pDP)
{
    IDirect3DDevice9 *pDevice = pDP->Device(0);

    HRESULT hr = S_OK;

    HTEST(pDevice->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xffafafaf, 1.0f, 0));

    HTEST(pDevice->BeginScene());

    HTEST(pDevice->SetStreamSource(0, mpVB, 0, sizeof(Vertex)));
    HTEST(pDevice->SetVertexDeclaration(mpVertexDecl));
    HTEST(pDevice->SetVertexShader(mpVS));
    HTEST(pDevice->SetPixelShader(mpPS));
    HTEST(pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));

    HTEST(pDevice->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1));

    HTEST(pDevice->EndScene());

    HTEST(pDevice->Present(0, 0, 0, 0));
    return S_OK;
}


/*
 * Cubemap texture.
 */

class D3D9RenderCubeMap: public D3D9Render
{
    public:
        D3D9RenderCubeMap();
        virtual ~D3D9RenderCubeMap();
        virtual HRESULT InitRender(D3D9DeviceProvider *pDP);
        virtual HRESULT DoRender(D3D9DeviceProvider *pDP);
        virtual void TimeAdvance(float dt);

    private:
        IDirect3DVertexDeclaration9 *mpVertexDecl;
        IDirect3DVertexBuffer9 *mpVertexBuffer;
        IDirect3DCubeTexture9 *mpCubeTexture;
        IDirect3DVertexShader9 *mpVS;
        IDirect3DPixelShader9 *mpPS;

        D3DCamera mCamera;

        static D3DVERTEXELEMENT9 maVertexElements[];
};

D3DVERTEXELEMENT9 D3D9RenderCubeMap::maVertexElements[] =
{
    { 0, 0,  D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
    D3DDECL_END()
};

D3D9RenderCubeMap::D3D9RenderCubeMap()
    :
    mpVertexDecl(0),
    mpVertexBuffer(0),
    mpCubeTexture(0),
    mpVS(0),
    mpPS(0)
{
}

D3D9RenderCubeMap::~D3D9RenderCubeMap()
{
    D3D_RELEASE(mpVS);
    D3D_RELEASE(mpPS);
    D3D_RELEASE(mpVertexBuffer);
    D3D_RELEASE(mpVertexDecl);
    D3D_RELEASE(mpCubeTexture);
}

HRESULT D3D9RenderCubeMap::InitRender(D3D9DeviceProvider *pDP)
{
    IDirect3DDevice9 *pDevice = pDP->Device(0);

    static const DWORD aVSCubeMap[] =
    {
        0xfffe0200,                                                             // vs_2_0
        0x05000051, 0xa00f0004, 0x3f800000, 0x00000000, 0x00000000, 0x00000000, // def c4, 1, 0, 0, 0
        0x0200001f, 0x80000000, 0x900f0000,                                     // dcl_position v0
        0x04000004, 0x800f0000, 0x90240000, 0xa0400004, 0xa0150004,             // mad r0, v0.xyzx, c4.xxxy, c4.yyyx
        0x03000009, 0x80010001, 0x80e40000, 0xa0e40000,                         // dp4 r1.x, r0, c0
        0x03000009, 0x80020001, 0x80e40000, 0xa0e40001,                         // dp4 r1.y, r0, c1
        0x03000009, 0x80040001, 0x80e40000, 0xa0e40003,                         // dp4 r1.z, r0, c3
        0x02000001, 0xc00f0000, 0x80a40001,                                     // mov oPos, r1.xyzz
        0x02000001, 0xe0070000, 0x90e40000,                                     // mov oT0.xyz, v0
        0x0000ffff
    };

    static const DWORD aPSCubeMap[] =
    {
        0xffff0200,                                      // ps_2_0
        0x0200001f, 0x80000000, 0xb0070000,              // dcl t0.xyz
        0x0200001f, 0x98000000, 0xa00f0800,              // dcl_cube s0
        0x03000042, 0x800f0000, 0xb0e40000, 0xa0e40800,  // texld r0, t0, s0
        0x02000001, 0x800f0800, 0x80e40000,              // mov oC0, r0
        0x0000ffff
    };

    HRESULT hr = S_OK;

    HTEST(pDevice->CreateVertexDeclaration(maVertexElements, &mpVertexDecl));
    HTEST(d3dCreateCubeVertexBuffer(pDevice, 1.0f, &mpVertexBuffer));
    HTEST(d3dCreateCubeTexture(pDevice, &mpCubeTexture));
    HTEST(pDevice->CreateVertexShader(aVSCubeMap, &mpVS));
    HTEST(pDevice->CreatePixelShader(aPSCubeMap, &mpPS));

    float w = 800;
    float h = 600;
    mCamera.SetProjection(3.14f * 0.5f, w/h, 1.0f, 100.0f);

    return S_OK;
}

HRESULT D3D9RenderCubeMap::DoRender(D3D9DeviceProvider *pDP)
{
    IDirect3DDevice9 *pDevice = pDP->Device(0);

    HRESULT hr = S_OK;

    // HTEST(pDevice->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff0000ff, 1.0f, 0));

    HTEST(pDevice->BeginScene());

    /* World matrix is identity matrix, i.e. need only View and Projection. */
    D3DMATRIX WVP = *mCamera.ViewProjection();
    d3dMatrixTranspose(&WVP); /* Because shader will multiply vector row by matrix column, i.e. columns must be in the shader constants. */

    HTEST(pDevice->SetVertexShader(mpVS));
    HTEST(pDevice->SetPixelShader(mpPS));

    HTEST(pDevice->SetVertexShaderConstantF(0, &WVP.m[0][0], 4));
    HTEST(pDevice->SetTexture(0, mpCubeTexture));

    HTEST(pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
    HTEST(pDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS));

    HTEST(pDevice->SetVertexDeclaration(mpVertexDecl));
    HTEST(pDevice->SetStreamSource(0, mpVertexBuffer, 0, 3 * sizeof(float)));

    HTEST(pDevice->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 6 * 2));

    HTEST(pDevice->EndScene());

    HTEST(pDevice->Present(0, 0, 0, 0));

    return S_OK;
}

void D3D9RenderCubeMap::TimeAdvance(float dt)
{
    mCamera.TimeAdvance(dt);
}


/*
 * Solid triangles using shaders and instancing.
 */

class D3D9RenderInstance: public D3D9Render
{
public:
    D3D9RenderInstance();
    virtual ~D3D9RenderInstance();
    virtual HRESULT InitRender(D3D9DeviceProvider *pDP);
    virtual HRESULT DoRender(D3D9DeviceProvider *pDP);
private:
    IDirect3DVertexBuffer9      *mpVBGeometry;
    IDirect3DVertexBuffer9      *mpVBInstance;
    IDirect3DIndexBuffer9       *mpIB;
    IDirect3DVertexDeclaration9 *mpVertexDecl;
    IDirect3DVertexShader9      *mpVS;
    IDirect3DPixelShader9       *mpPS;

    struct VertexGeometry
    {
        D3DVECTOR position;
    };

    struct VertexInstance
    {
        D3DCOLOR  color;
        float dy;
    };
    static D3DVERTEXELEMENT9 VertexElements[];
};

D3DVERTEXELEMENT9 D3D9RenderInstance::VertexElements[] =
{
    {0, GA_W_OFFSET_OF(VertexGeometry, position), D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {1, GA_W_OFFSET_OF(VertexInstance, color),    D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0},
    {1, GA_W_OFFSET_OF(VertexInstance, dy),       D3DDECLTYPE_FLOAT1,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    D3DDECL_END()
};


D3D9RenderInstance::D3D9RenderInstance()
    :
    mpVBGeometry(0),
    mpVBInstance(0),
    mpIB(0),
    mpVertexDecl(0),
    mpVS(0),
    mpPS(0)
{
}

D3D9RenderInstance::~D3D9RenderInstance()
{
    D3D_RELEASE(mpVBGeometry);
    D3D_RELEASE(mpVBInstance);
    D3D_RELEASE(mpIB);
    D3D_RELEASE(mpVertexDecl);
    D3D_RELEASE(mpVS);
    D3D_RELEASE(mpPS);
}

HRESULT D3D9RenderInstance::InitRender(D3D9DeviceProvider *pDP)
{
    IDirect3DDevice9 *pDevice = pDP->Device(0);

    static DWORD aVSCode[] =
    {
        0xfffe0200,                                     // vs_2_0
        0x05000051, 0xa00f0000, 0x3f800000, 0x00000000, 0x00000000, 0x00000000, // def c0, 1, 0, 0, 0
        0x0200001f, 0x80000000, 0x900f0000,             // dcl_position v0
        0x0200001f, 0x8000000a, 0x900f0001,             // dcl_color v1
        0x0200001f, 0x80000005, 0x900f0002,             // dcl_texcoord v2
        0x02000001, 0x80020000, 0x90550000,             // mov r0.y, v0.y
        0x03000002, 0xc0020000, 0x80550000, 0x90000002, // add oPos.y, r0.y, v2.x
        0x02000001, 0xc0050000, 0x90e40000,             // mov oPos.xz, v0
        0x02000001, 0xc0080000, 0xa0000000,             // mov oPos.w, c0.x
        0x02000001, 0xd00f0000, 0x90e40001,             // mov oD0, v1
        0x0000ffff
    };

    static DWORD aPSCode[] =
    {
        0xFFFF0200,                         // ps_2_0
        0x0200001f, 0x80000000, 0x900f0000, // dcl v0
        0x02000001, 0x800f0800, 0x90e40000, // mov oC0, v0
        0x0000FFFF
    };

    static VertexGeometry aVerticesGeometry[] =
    {
        { { -0.5f, -0.5f, 0.5f } },
        { {  0.5f, -0.5f, 0.5f } },
        { {  0.0f,  0.5f, 0.5f } },
    };

    static VertexInstance aVerticesInstance[] =
    {
        { D3DCOLOR_XRGB(  0,   0, 255), -0.5f },
        { D3DCOLOR_XRGB(  0, 255,   0),  0.0f },
        { D3DCOLOR_XRGB(255,   0,   0),  0.5f },
    };

    static USHORT aIndices[] =
    {
        0, 1, 2
    };

    HRESULT hr = S_OK;

    HTEST(pDevice->CreateVertexDeclaration(VertexElements, &mpVertexDecl));

    HTEST(pDevice->CreateVertexBuffer(sizeof(aVerticesGeometry),
                                      0, /* D3DUSAGE_* */
                                      0, /* FVF */
                                      D3DPOOL_DEFAULT,
                                      &mpVBGeometry,
                                      0));
    HTEST(pDevice->CreateVertexBuffer(sizeof(aVerticesInstance),
                                      0, /* D3DUSAGE_* */
                                      0, /* FVF */
                                      D3DPOOL_DEFAULT,
                                      &mpVBInstance,
                                      0));

    HTEST(pDevice->CreateIndexBuffer(sizeof(aIndices),
                                      0, /* D3DUSAGE_* */
                                      D3DFMT_INDEX16,
                                      D3DPOOL_DEFAULT,
                                      &mpIB,
                                      0));

    HTEST(pDevice->CreateVertexShader(aVSCode, &mpVS));
    HTEST(pDevice->CreatePixelShader(aPSCode, &mpPS));

    HTEST(d3dCopyToVertexBuffer(mpVBGeometry, aVerticesGeometry, sizeof(aVerticesGeometry)));
    HTEST(d3dCopyToVertexBuffer(mpVBInstance, aVerticesInstance, sizeof(aVerticesInstance)));
    HTEST(d3dCopyToIndexBuffer(mpIB, aIndices, sizeof(aIndices)));

    return hr;
}

HRESULT D3D9RenderInstance::DoRender(D3D9DeviceProvider *pDP)
{
    IDirect3DDevice9 *pDevice = pDP->Device(0);

    HRESULT hr = S_OK;

    HTEST(pDevice->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xffafafaf, 1.0f, 0));

    HTEST(pDevice->BeginScene());

    HTEST(pDevice->SetStreamSource(0, mpVBGeometry, 0, sizeof(VertexGeometry)));
    /* Draw 2 instances, which should produce a solid blue triangle and a green one. */
    HTEST(pDevice->SetStreamSourceFreq(0, D3DSTREAMSOURCE_INDEXEDDATA | 2U));

    HTEST(pDevice->SetStreamSource(1, mpVBInstance, 0, sizeof(VertexInstance)));
    HTEST(pDevice->SetStreamSourceFreq(1, D3DSTREAMSOURCE_INSTANCEDATA | 1U));

    HTEST(pDevice->SetVertexDeclaration(mpVertexDecl));
    HTEST(pDevice->SetVertexShader(mpVS));
    HTEST(pDevice->SetPixelShader(mpPS));
    HTEST(pDevice->SetIndices(mpIB));
    HTEST(pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));

    HTEST(pDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,
                                        0,  /* BaseVertexIndex */
                                        0,  /* MinIndex */
                                        3,  /* NumVertices */
                                        0,  /* StartIndex */
                                        1)); /* PrimitiveCount */

    HTEST(pDevice->SetStreamSourceFreq(0, 1));
    HTEST(pDevice->SetStreamSourceFreq(1, 1));

    HTEST(pDevice->EndScene());

    HTEST(pDevice->Present(0, 0, 0, 0));
    return S_OK;
}


/*
 * Solid triangles with different Z positions.
 */

class D3D9RenderDepth: public D3D9Render
{
public:
    D3D9RenderDepth();
    virtual ~D3D9RenderDepth();
    virtual HRESULT InitRender(D3D9DeviceProvider *pDP);
    virtual HRESULT DoRender(D3D9DeviceProvider *pDP);
private:
    IDirect3DVertexBuffer9      *mpVB;
    IDirect3DVertexDeclaration9 *mpVertexDecl;
    IDirect3DVertexShader9      *mpVS;
    IDirect3DPixelShader9       *mpPS;

    D3DCamera mCamera;

    struct Vertex
    {
        D3DVECTOR position;
    };
    static D3DVERTEXELEMENT9 VertexElements[];
};

D3DVERTEXELEMENT9 D3D9RenderDepth::VertexElements[] =
{
    {0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    D3DDECL_END()
};


D3D9RenderDepth::D3D9RenderDepth()
    :
    mpVB(0),
    mpVertexDecl(0),
    mpVS(0),
    mpPS(0)
{
}

D3D9RenderDepth::~D3D9RenderDepth()
{
    D3D_RELEASE(mpVS);
    D3D_RELEASE(mpPS);
    D3D_RELEASE(mpVB);
    D3D_RELEASE(mpVertexDecl);
}

HRESULT D3D9RenderDepth::InitRender(D3D9DeviceProvider *pDP)
{
    IDirect3DDevice9 *pDevice = pDP->Device(0);

    static DWORD aVSCode[] =
    {
        0xfffe0200,                                                             // vs_2_0
        0x05000051, 0xa00f0005, 0x3f800000, 0x00000000, 0x00000000, 0x00000000, // def c5, 1, 0, 0, 0
        0x0200001f, 0x80000000, 0x900f0000,                                     // dcl_position v0
        0x04000004, 0x800f0000, 0x90240000, 0xa0400005, 0xa0150005,             // mad r0, v0.xyzx, c5.xxxy, c5.yyyx
        0x03000009, 0xc0010000, 0x80e40000, 0xa0e40000,                         // dp4 oPos.x, r0, c0
        0x03000009, 0xc0020000, 0x80e40000, 0xa0e40001,                         // dp4 oPos.y, r0, c1
        0x03000009, 0xc0040000, 0x80e40000, 0xa0e40002,                         // dp4 oPos.z, r0, c2
        0x03000009, 0xc0080000, 0x80e40000, 0xa0e40003,                         // dp4 oPos.w, r0, c3
        0x02000001, 0xd00f0000, 0xa0e40004,                                     // mov oD0, c4
        0x0000ffff
    };

    static DWORD aPSCode[] =
    {
        0xFFFF0200,                         // ps_2_0
        0x0200001f, 0x80000000, 0x900f0000, // dcl v0
        0x02000001, 0x800f0800, 0x90e40000, // mov oC0, v0
        0x0000FFFF
    };

    static Vertex aVertices[] =
    {
        { { -1.0f, -1.0f, 0.0f } },
        { {  1.0f, -1.0f, 0.0f } },
        { {  0.0f,  1.0f, 0.0f } },
    };

    HRESULT hr = S_OK;

    HTEST(pDevice->CreateVertexDeclaration(VertexElements, &mpVertexDecl));
    HTEST(pDevice->CreateVertexBuffer(sizeof(aVertices),
                                      0, /* D3DUSAGE_* */
                                      0, /* FVF */
                                      D3DPOOL_DEFAULT,
                                      &mpVB,
                                      0));
    HTEST(pDevice->CreateVertexShader(aVSCode, &mpVS));
    HTEST(pDevice->CreatePixelShader(aPSCode, &mpPS));

    HTEST(d3dCopyToVertexBuffer(mpVB, aVertices, sizeof(aVertices)));

    const D3DVECTOR cameraPosition = { 0.0f, 0.0f, -10.0f };
    const D3DVECTOR cameraAt = { 0.0f, 0.0f, 1.0f };
    const D3DVECTOR cameraUp = { 0.0f, 1.0f, 0.0f };
    mCamera.SetupAt(&cameraPosition, &cameraAt, &cameraUp);

    float w = 800;
    float h = 600;
    mCamera.SetProjection(3.14f * 0.5f, w/h, 1.0f, 100.0f);

    return hr;
}

static void d3d9RenderDepthDrawTriangle(IDirect3DDevice9 *pDevice, D3DCamera *pCamera,
                                        float s, float x, float y, float z, D3DCOLOR color)
{
    HRESULT hr; /* Used by HTEST. */

    /* Calc world matrix. */
    D3DMATRIX mtxST;
    d3dMatrixScaleTranslation(&mtxST, s, x, y, z);

    D3DMATRIX WVP;
    d3dMatrixMultiply(&WVP, &mtxST, pCamera->ViewProjection());

    /* Transpose, because shader will multiply vector row by matrix column,
     * i.e. columns must be in the shader constants.
     */
    d3dMatrixTranspose(&WVP);

    HTEST(pDevice->SetVertexShaderConstantF(0, &WVP.m[0][0], 4));

    /* Set color of the triangle. */
    float aColor[4];
    aColor[0] = 1.0f * (float)((color >> 16) & 0xff) / 255.0f;
    aColor[1] = 1.0f * (float)((color >>  8) & 0xff) / 255.0f;
    aColor[2] = 1.0f * (float)((color      ) & 0xff) / 255.0f;
    aColor[3] = 1.0f;
    HTEST(pDevice->SetVertexShaderConstantF(4, &aColor[0], 1));

    HTEST(pDevice->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1));
}

HRESULT D3D9RenderDepth::DoRender(D3D9DeviceProvider *pDP)
{
    IDirect3DDevice9 *pDevice = pDP->Device(0);

    HRESULT hr;

    HTEST(pDevice->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff7f7f7f, 1.0f, 0));

    D3DVIEWPORT9 viewport;
    HTEST(pDevice->GetViewport(&viewport));

    HTEST(pDevice->SetVertexShader(mpVS));
    HTEST(pDevice->SetPixelShader(mpPS));

    HTEST(pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
    HTEST(pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE));

    HTEST(pDevice->SetVertexDeclaration(mpVertexDecl));
    HTEST(pDevice->SetStreamSource(0, mpVB, 0, 3 * sizeof(float)));

    HTEST(pDevice->BeginScene());

    viewport.MinZ = 0.2f;
    viewport.MaxZ = 0.3f;
    HTEST(pDevice->SetViewport(&viewport));

    d3d9RenderDepthDrawTriangle(pDevice, &mCamera, 20.0f, -50.0f, -20.0f, 50.0f, D3DCOLOR_XRGB(0, 128, 0));
    d3d9RenderDepthDrawTriangle(pDevice, &mCamera, 20.0f, -40.0f, -10.0f, 55.0f, D3DCOLOR_XRGB(0, 0, 128));

    viewport.MinZ = 0.9f;
    viewport.MaxZ = 1.0f;
    HTEST(pDevice->SetViewport(&viewport));

    d3d9RenderDepthDrawTriangle(pDevice, &mCamera, 20.0f, -50.0f, 0.0f, 40.0f, D3DCOLOR_XRGB(0, 255, 0));
    d3d9RenderDepthDrawTriangle(pDevice, &mCamera, 20.0f, -45.0f, 0.0f, 45.0f, D3DCOLOR_XRGB(0, 0, 255));

    HTEST(pDevice->EndScene());

    HTEST(pDevice->Present(0, 0, 0, 0));

    return S_OK;
}


/*
 * Shared resource and render to texture.
 */

class D3D9RenderShared: public D3D9Render
{
public:
    D3D9RenderShared();
    virtual ~D3D9RenderShared();
    virtual int RequiredDeviceCount() { return 2; }
    virtual HRESULT InitRender(D3D9DeviceProvider *pDP);
    virtual HRESULT DoRender(D3D9DeviceProvider *pDP);
private:
    IDirect3DVertexBuffer9      *mpVB;
    IDirect3DVertexDeclaration9 *mpVertexDecl;
    IDirect3DVertexShader9      *mpVS;
    IDirect3DPixelShader9       *mpPS;
    IDirect3DTexture9           *mpRT;
    IDirect3DTexture9           *mpTexShared;

    HANDLE mhRtShared;
    DWORD mdwRtWidth;
    DWORD mdwRtHeight;

    struct Vertex
    {
        D3DVECTOR position;
        D3DCOLOR  color;
    };
    static D3DVERTEXELEMENT9 VertexElements[];

    void renderToTexture(IDirect3DDevice9 *pDevice, IDirect3DTexture9 *pTexture);
};

D3DVERTEXELEMENT9 D3D9RenderShared::VertexElements[] =
{
    {0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0},
    D3DDECL_END()
};


D3D9RenderShared::D3D9RenderShared()
    :
    mpVB(0),
    mpVertexDecl(0),
    mpVS(0),
    mpPS(0),
    mpRT(0),
    mpTexShared(0)
{
    mdwRtWidth = 640;
    mdwRtHeight = 480;
}

D3D9RenderShared::~D3D9RenderShared()
{
    D3D_RELEASE(mpVS);
    D3D_RELEASE(mpPS);
    D3D_RELEASE(mpVB);
    D3D_RELEASE(mpVertexDecl);
    D3D_RELEASE(mpTexShared);
    D3D_RELEASE(mpRT);
}

HRESULT D3D9RenderShared::InitRender(D3D9DeviceProvider *pDP)
{
    IDirect3DDevice9 *pDevice = pDP->Device(0);

    static DWORD aVSCode[] =
    {
        0xFFFE0200,                         // vs_2_0
        0x05000051, 0xa00f0000, 0x3f800000, 0x00000000, 0x00000000, 0x00000000, // def c0, 1, 0, 0, 0
        0x0200001f, 0x80000000, 0x900f0000, // dcl_position v0
        0x0200001f, 0x8000000a, 0x900f0001, // dcl_color v1
        0x02000001, 0xc0070000, 0x90e40000, // mov oPos.xyz, v0
        0x02000001, 0xc0080000, 0xa0000000, // mov oPos.w, c0.x
        0x02000001, 0xd00f0000, 0x90e40001, // mov oD0, v1
        0x0000FFFF
    };

    static DWORD aPSCode[] =
    {
        0xFFFF0200,                         // ps_2_0
        0x0200001f, 0x80000000, 0x900f0000, // dcl v0
        0x02000001, 0x800f0800, 0x90e40000, // mov oC0, v0
        0x0000FFFF
    };

    static Vertex aVertices[] =
    {
        { { -0.5f, -0.5f, 0.5f }, D3DCOLOR_XRGB(  0,   0, 255), },
        { {  0.5f, -0.5f, 0.5f }, D3DCOLOR_XRGB(  0, 255,   0), },
        { {  0.0f,  0.5f, 0.5f }, D3DCOLOR_XRGB(255,   0,   0), },
    };

    HRESULT hr = S_OK;

    HTEST(pDevice->CreateVertexDeclaration(VertexElements, &mpVertexDecl));
    HTEST(pDevice->CreateVertexBuffer(sizeof(aVertices),
                                      0, /* D3DUSAGE_* */
                                      0, /* FVF */
                                      D3DPOOL_DEFAULT,
                                      &mpVB,
                                      0));
    HTEST(pDevice->CreateVertexShader(aVSCode, &mpVS));
    HTEST(pDevice->CreatePixelShader(aPSCode, &mpPS));

    HTEST(d3dCopyToVertexBuffer(mpVB, aVertices, sizeof(aVertices)));

    mhRtShared = 0;
    HTEST(pDevice->CreateTexture(mdwRtWidth,
                                 mdwRtHeight,
                                 1,
                                 D3DUSAGE_RENDERTARGET,
                                 D3DFMT_X8R8G8B8,
                                 D3DPOOL_DEFAULT,
                                 &mpRT,
                                 &mhRtShared));

    return hr;
}

void D3D9RenderShared::renderToTexture(IDirect3DDevice9 *pDevice, IDirect3DTexture9 *pTexture)
{
    HRESULT hr = S_OK;

    /*
     * Render to texture.
     */
    IDirect3DSurface9 *pSavedRT = NULL;
    HTEST(pDevice->GetRenderTarget(0, &pSavedRT));

    IDirect3DSurface9 *pSurface = NULL;
    HTEST(pTexture->GetSurfaceLevel(0, &pSurface));
    HTEST(pDevice->SetRenderTarget(0, pSurface));

    HTEST(pDevice->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xffafafaf, 1.0f, 0));

    HTEST(pDevice->BeginScene());

    HTEST(pDevice->SetStreamSource(0, mpVB, 0, sizeof(Vertex)));
    HTEST(pDevice->SetVertexDeclaration(mpVertexDecl));
    HTEST(pDevice->SetVertexShader(mpVS));
    HTEST(pDevice->SetPixelShader(mpPS));
    HTEST(pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));

    HTEST(pDevice->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1));

    HTEST(pDevice->EndScene());

    HTEST(pDevice->SetRenderTarget(0, pSavedRT));

    D3D_RELEASE(pSurface);
}

static void issueQuery(IDirect3DDevice9 *pDevice)
{
    HRESULT hr;

    IDirect3DQuery9 *pQuery;
    hr = pDevice->CreateQuery(D3DQUERYTYPE_EVENT, &pQuery);
    if (hr == D3D_OK)
    {
        hr = pQuery->Issue(D3DISSUE_END);
        if (hr == D3D_OK)
        {
            do
            {
                hr = pQuery->GetData(NULL, 0, D3DGETDATA_FLUSH);
            } while (hr == S_FALSE);
        }

        D3D_RELEASE(pQuery);
    }
}

HRESULT D3D9RenderShared::DoRender(D3D9DeviceProvider *pDP)
{
    HRESULT hr = S_OK;

    IDirect3DDevice9 *pDevice = pDP->Device(0);

    renderToTexture(pDevice, mpRT);

    issueQuery(pDevice);

    IDirect3DDevice9 *pDevice2 = pDP->Device(1);
    if (pDevice2)
    {
        HTEST(pDevice2->CreateTexture(mdwRtWidth,
                                      mdwRtHeight,
                                      1,
                                      D3DUSAGE_RENDERTARGET,
                                      D3DFMT_X8R8G8B8,
                                      D3DPOOL_DEFAULT,
                                      &mpTexShared,
                                      &mhRtShared));

        drawTexture(pDevice2, mpTexShared, 50, 50, 200, 200);
        HTEST(pDevice2->Present(0, 0, 0, 0));
    }
    else
    {
        drawTexture(pDevice, mpRT, 50, 50, 200, 200);
        HTEST(pDevice->Present(0, 0, 0, 0));
    }

    return S_OK;
}


/*
 * Use ColorFill to clear a part of the backbuffer.
 */

class D3D9RenderColorFill: public D3D9Render
{
public:
    D3D9RenderColorFill() {}
    virtual ~D3D9RenderColorFill() {}
    virtual HRESULT InitRender(D3D9DeviceProvider *pDP);
    virtual HRESULT DoRender(D3D9DeviceProvider *pDP);
};

HRESULT D3D9RenderColorFill::InitRender(D3D9DeviceProvider *pDP)
{
    (void)pDP;
    return S_OK;
}

HRESULT D3D9RenderColorFill::DoRender(D3D9DeviceProvider *pDP)
{
    HRESULT hr = S_OK;

    IDirect3DDevice9 *pDevice = pDP->Device(0);

    pDevice->Clear(0, 0, D3DCLEAR_TARGET, 0xff0000ff, 0.0f, 0);

    IDirect3DSurface9 *pBackBuffer = NULL;
    HTEST(pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer));

    RECT rDst;
    rDst.left = 50;
    rDst.top = 10;
    rDst.right = 250;
    rDst.bottom = 250;

    HTEST(pDevice->ColorFill(pBackBuffer, &rDst, D3DCOLOR_XRGB(0, 255, 0)));

    D3D_RELEASE(pBackBuffer);

    HTEST(pDevice->Present(0, 0, 0, 0));
    return S_OK;
}


/*
 * Render a texture to texture.
 */

class D3D9RenderTexture: public D3D9Render
{
public:
    D3D9RenderTexture();
    virtual ~D3D9RenderTexture();
    virtual HRESULT InitRender(D3D9DeviceProvider *pDP);
    virtual HRESULT DoRender(D3D9DeviceProvider *pDP);
private:
    IDirect3DVertexBuffer9      *mpVB;
    IDirect3DVertexDeclaration9 *mpVertexDecl;
    IDirect3DVertexShader9      *mpVS;
    IDirect3DPixelShader9       *mpPS;
    IDirect3DTexture9           *mpTexDst;
    IDirect3DTexture9           *mpTexSrc;

    static const int cxTexture = 8;
    static const int cyTexture = 8;

    struct Vertex
    {
        D3DVECTOR position;
        float     x, y;
    };
    static D3DVERTEXELEMENT9 VertexElements[];

    void renderToTexture(IDirect3DDevice9 *pDevice, IDirect3DTexture9 *pTexture, IDirect3DTexture9 *pTexSrc);
};

D3DVERTEXELEMENT9 D3D9RenderTexture::VertexElements[] =
{
    {0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 12, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    D3DDECL_END()
};


D3D9RenderTexture::D3D9RenderTexture()
    :
    mpVB(0),
    mpVertexDecl(0),
    mpVS(0),
    mpPS(0),
    mpTexDst(0),
    mpTexSrc(0)
{
}

D3D9RenderTexture::~D3D9RenderTexture()
{
    D3D_RELEASE(mpVS);
    D3D_RELEASE(mpPS);
    D3D_RELEASE(mpVB);
    D3D_RELEASE(mpVertexDecl);
    D3D_RELEASE(mpTexSrc);
    D3D_RELEASE(mpTexDst);
}

HRESULT D3D9RenderTexture::InitRender(D3D9DeviceProvider *pDP)
{
    IDirect3DDevice9 *pDevice = pDP->Device(0);

    static DWORD aVSCode[] =
    {
        0xFFFE0200,                                     // vs_2_0
        0x05000051, 0xa00f0000, 0x3f800000, 0x00000000, 0x00000000, 0x00000000, // def c0, 1, 0, 0, 0
        0x0200001f, 0x80000000, 0x900f0000,             // dcl_position v0
        0x0200001f, 0x80000005, 0x900f0001,             // dcl_texcoord v1
        0x02000001, 0xc0070000, 0x90e40000,             // mov oPos.xyz, v0
        0x02000001, 0xc0080000, 0xa0000000,             // mov oPos.w, c0.x
        0x02000001, 0xe0030000, 0x90e40001,             // mov oT0.xy, v1
        0x0000FFFF
    };

    static DWORD aVSCodeMad[] =
    {
        0xFFFE0200,                                                             // vs_2_0
        0x05000051, 0xa00f0000, 0x3f800000, 0x00000000, 0x00000000, 0x00000000, // def c0, 1, 0, 0, 0
        0x0200001f, 0x80000000, 0x900f0000,                                     // dcl_position v0
        0x0200001f, 0x80000005, 0x900f0001,                                     // dcl_texcoord v1
        0x04000004, 0xc00f0000, 0x90240000, 0xa0400000, 0xa0150000,             // mad oPos, v0.xyzx, c0.xxxy, c0.yyyx
        0x02000001, 0xe0030000, 0x90e40001,                                     // mov oT0.xy, v1
        0x0000FFFF
    };

    static DWORD aPSCodeSwap[] =
    {
        0xffff0200,                                     // ps_2_0
        0x0200001f, 0x80000000, 0xb0030000,             // dcl t0.xy
        0x0200001f, 0x90000000, 0xa00f0800,             // dcl_2d s0
        0x03000042, 0x800f0000, 0xb0e40000, 0xa0e40800, // texld r0, t0, s0
        0x02000001, 0x80090001, 0x80d20000,             // mov r1.xw, r0.zxyw
        0x02000001, 0x80040001, 0x80000000,             // mov r1.z, r0.x
        0x02000001, 0x80020001, 0x80550000,             // mov r1.y, r0.y
        0x02000001, 0x800f0800, 0x80e40001,             // mov oC0, r1
        0x0000ffff
    };

    static DWORD aPSCodePass[] =
    {
        0xffff0200,                                     // ps_2_0
        0x0200001f, 0x80000000, 0xb0030000,             // dcl t0.xy
        0x0200001f, 0x90000000, 0xa00f0800,             // dcl_2d s0
        0x03000042, 0x800f0000, 0xb0e40000, 0xa0e40800, // texld r0, t0, s0
        0x02000001, 0x800f0800, 0x80e40000,             // mov oC0, r0
        0x0000ffff
    };

    static DWORD aPSCodeCoord[] =
    {
        0xffff0200,                                     // ps_2_0
        0x0200001f, 0x80000000, 0xb0010000,             // dcl t0.x
        0x02000001, 0x800f0000, 0xb0000000,             // mov r0, t0.x
        0x02000001, 0x800f0800, 0x80e40000,             // mov oC0, r0
        0x0000ffff
    };

    static Vertex aVertices[] =
    {
        { { -1.0f, -1.0f, 0.0f }, 0.0f, 1.0f},
        { {  1.0f, -1.0f, 0.0f }, 1.0f, 1.0f},
        { { -1.0f,  1.0f, 0.0f }, 0.0f, 0.0f},

        { { -1.0f,  1.0f, 0.0f }, 0.0f, 0.0f},
        { {  1.0f, -1.0f, 0.0f }, 1.0f, 1.0f},
        { {  1.0f,  1.0f, 0.0f }, 1.0f, 0.0f},
    };

    HRESULT hr = S_OK;

    HTEST(pDevice->CreateVertexDeclaration(VertexElements, &mpVertexDecl));
    HTEST(pDevice->CreateVertexBuffer(sizeof(aVertices),
                                      0, /* D3DUSAGE_* */
                                      0, /* FVF */
                                      D3DPOOL_DEFAULT,
                                      &mpVB,
                                      0));
    HTEST(pDevice->CreateVertexShader(aVSCodeMad, &mpVS));
    HTEST(pDevice->CreatePixelShader(aPSCodeSwap, &mpPS));

    HTEST(d3dCopyToVertexBuffer(mpVB, aVertices, sizeof(aVertices)));

    HTEST(pDevice->CreateTexture(cxTexture,
                                 cyTexture,
                                 1,
                                 D3DUSAGE_RENDERTARGET,
                                 D3DFMT_A8R8G8B8,
                                 D3DPOOL_DEFAULT,
                                 &mpTexDst,
                                 NULL));

    HTEST(pDevice->CreateTexture(cxTexture,
                                 cyTexture,
                                 1,
                                 D3DUSAGE_DYNAMIC,
                                 D3DFMT_A8R8G8B8,
                                 D3DPOOL_DEFAULT,
                                 &mpTexSrc,
                                 NULL));

    D3DLOCKED_RECT LockedRect;
    HTEST(mpTexSrc->LockRect(0, &LockedRect, NULL /* entire texture */, 0));

    unsigned char *pScanline = (unsigned char *)LockedRect.pBits;
    for (UINT y = 0; y < cxTexture; ++y)
    {
        for (UINT x = 0; x < cxTexture; ++x)
        {
            if (x < y)
            {
                pScanline[x * 4 + 0] = 0xff;
                pScanline[x * 4 + 1] = 0x00;
                pScanline[x * 4 + 2] = 0x00;
                pScanline[x * 4 + 3] = 0x00;
            }
            else
            {
                pScanline[x * 4 + 0] = 0x00;
                pScanline[x * 4 + 1] = 0x00;
                pScanline[x * 4 + 2] = 0xff;
                pScanline[x * 4 + 3] = 0x00;
            }
        }

        pScanline += LockedRect.Pitch;
    }

    HTEST(mpTexSrc->UnlockRect(0));

    return hr;
}

void D3D9RenderTexture::renderToTexture(IDirect3DDevice9 *pDevice, IDirect3DTexture9 *pTexture, IDirect3DTexture9 *pTexSrc)
{
    HRESULT hr = S_OK;

    /*
     * Render to texture.
     */
    IDirect3DSurface9 *pSavedRT = NULL;
    HTEST(pDevice->GetRenderTarget(0, &pSavedRT));

    IDirect3DSurface9 *pSurface = NULL;
    HTEST(pTexture->GetSurfaceLevel(0, &pSurface));
    HTEST(pDevice->SetRenderTarget(0, pSurface));

    HTEST(pDevice->BeginScene());

    HTEST(pDevice->SetStreamSource(0, mpVB, 0, sizeof(Vertex)));
    HTEST(pDevice->SetVertexDeclaration(mpVertexDecl));
    HTEST(pDevice->SetVertexShader(mpVS));
    HTEST(pDevice->SetPixelShader(mpPS));
    HTEST(pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
    HTEST(pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE));

    HTEST(pDevice->SetTexture(0, pTexSrc));

    HTEST(pDevice->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2));

    HTEST(pDevice->EndScene());

    HTEST(pDevice->SetRenderTarget(0, pSavedRT));

    D3D_RELEASE(pSurface);
}

HRESULT D3D9RenderTexture::DoRender(D3D9DeviceProvider *pDP)
{
    HRESULT hr = S_OK;

    IDirect3DDevice9 *pDevice = pDP->Device(0);

    renderToTexture(pDevice, mpTexDst, mpTexSrc);

    drawTexture(pDevice, mpTexDst, 50, 50, 200, 200);

    HTEST(pDevice->Present(0, 0, 0, 0));

    return S_OK;
}


/*
 * Render a texture to screen.
 */

class D3D9RenderTextureToScreen: public D3D9Render
{
public:
    D3D9RenderTextureToScreen(float cPixelShift);
    virtual ~D3D9RenderTextureToScreen();
    virtual HRESULT InitRender(D3D9DeviceProvider *pDP);
    virtual HRESULT DoRender(D3D9DeviceProvider *pDP);
private:
    IDirect3DVertexBuffer9      *mpVB;
    IDirect3DVertexDeclaration9 *mpVertexDecl;
    IDirect3DVertexShader9      *mpVS;
    IDirect3DPixelShader9       *mpPS;
    IDirect3DTexture9           *mpTex;

    static const int cxTexture = 4;
    static const int cyTexture = 4;
    float mcPixelShift;

    struct Vertex
    {
        D3DVECTOR position;
        float     x, y;
    };
    static D3DVERTEXELEMENT9 VertexElements[];
    static DWORD adwTextureData[cxTexture * cyTexture];
};

D3DVERTEXELEMENT9 D3D9RenderTextureToScreen::VertexElements[] =
{
    {0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 12, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    D3DDECL_END()
};

DWORD D3D9RenderTextureToScreen::adwTextureData[cxTexture * cyTexture] =
{
    0x000000FF, 0x0000FF00, 0x00FF0000, 0x00FFFFFF,
    0x0000FF00, 0x00FF0000, 0x000000FF, 0x00FFFFFF,
    0x00FF0000, 0x000000FF, 0x0000FF00, 0x00FFFFFF,
    0x0000FFFF, 0x00FF00FF, 0x00FFFF00, 0x00FFFFFF,
};


D3D9RenderTextureToScreen::D3D9RenderTextureToScreen(float cPixelShift)
    :
    mpVB(0),
    mpVertexDecl(0),
    mpVS(0),
    mpPS(0),
    mpTex(0),
    mcPixelShift(cPixelShift)
{
}

D3D9RenderTextureToScreen::~D3D9RenderTextureToScreen()
{
    D3D_RELEASE(mpVS);
    D3D_RELEASE(mpPS);
    D3D_RELEASE(mpVB);
    D3D_RELEASE(mpVertexDecl);
    D3D_RELEASE(mpTex);
}

HRESULT D3D9RenderTextureToScreen::InitRender(D3D9DeviceProvider *pDP)
{
    IDirect3DDevice9 *pDevice = pDP->Device(0);

    static DWORD aVSCode[] =
    {
        0xFFFE0200,                                     // vs_2_0
        0x05000051, 0xa00f0000, 0x3f800000, 0x00000000, 0x00000000, 0x00000000, // def c0, 1, 0, 0, 0
        0x0200001f, 0x80000000, 0x900f0000,             // dcl_position v0
        0x0200001f, 0x80000005, 0x900f0001,             // dcl_texcoord v1
        0x02000001, 0xc0070000, 0x90e40000,             // mov oPos.xyz, v0
        0x02000001, 0xc0080000, 0xa0000000,             // mov oPos.w, c0.x
        0x02000001, 0xe0030000, 0x90e40001,             // mov oT0.xy, v1
        0x0000FFFF
    };

    static DWORD aPSCodePass[] =
    {
        0xffff0200,                                     // ps_2_0
        0x0200001f, 0x80000000, 0xb0030000,             // dcl t0.xy
        0x0200001f, 0x90000000, 0xa00f0800,             // dcl_2d s0
        0x03000042, 0x800f0000, 0xb0e40000, 0xa0e40800, // texld r0, t0, s0
        0x02000001, 0x800f0800, 0x80e40000,             // mov oC0, r0
        0x0000ffff
    };

    static DWORD aPSCodeCoord[] =
    {
        0xffff0200,                                     // ps_2_0
        0x0200001f, 0x80000000, 0xb0010000,             // dcl t0.x
        0x02000001, 0x800f0000, 0xb0000000,             // mov r0, t0.x
        0x02000001, 0x800f0800, 0x80e40000,             // mov oC0, r0
        0x0000ffff
    };

    D3DVIEWPORT9 vp;
    pDevice->GetViewport(&vp);

    int x1 = 1;
    int y1 = 1;
    int x2 = x1 + cxTexture * 1;
    int y2 = y1 + cyTexture * 1;

    float cPixelShift = mcPixelShift;

    #define VX(x) ( 2.0f * (float(x) + cPixelShift) / float(vp.Width)  - 1.0f)
    #define VY(y) (-2.0f * (float(y) + cPixelShift) / float(vp.Height) + 1.0f)

    Vertex vTopLeft     = { { VX(x1), VY(y1), 0.0f }, 0.0f, 0.0f};
    Vertex vBottomLeft  = { { VX(x1), VY(y2), 0.0f }, 0.0f, 1.0f};
    Vertex vTopRight    = { { VX(x2), VY(y1), 0.0f }, 1.0f, 0.0f};
    Vertex vBottomRight = { { VX(x2), VY(y2), 0.0f }, 1.0f, 1.0f};

    Vertex aVertices[6] =
    {
        vBottomLeft, vBottomRight, vTopLeft,
        vTopLeft, vBottomRight, vTopRight
    };

    HRESULT hr = S_OK;

    HTEST(pDevice->CreateVertexDeclaration(VertexElements, &mpVertexDecl));
    HTEST(pDevice->CreateVertexBuffer(sizeof(aVertices),
                                      0, /* D3DUSAGE_* */
                                      0, /* FVF */
                                      D3DPOOL_DEFAULT,
                                      &mpVB,
                                      0));
    HTEST(pDevice->CreateVertexShader(aVSCode, &mpVS));
    HTEST(pDevice->CreatePixelShader(aPSCodePass, &mpPS));

    HTEST(d3dCopyToVertexBuffer(mpVB, aVertices, sizeof(aVertices)));

    HTEST(pDevice->CreateTexture(cxTexture,
                                 cyTexture,
                                 1,
                                 D3DUSAGE_DYNAMIC,
                                 D3DFMT_A8R8G8B8,
                                 D3DPOOL_DEFAULT,
                                 &mpTex,
                                 NULL));

    D3DLOCKED_RECT LockedRect;
    HTEST(mpTex->LockRect(0, &LockedRect, NULL /* entire texture */, D3DLOCK_DISCARD));

    unsigned char *pu8Dst = (unsigned char *)LockedRect.pBits;
    for (int y = 0; y < cyTexture; ++y)
    {
         memcpy(pu8Dst, &adwTextureData[y * cxTexture], 4 * cxTexture);
         pu8Dst += LockedRect.Pitch;
    }

    HTEST(mpTex->UnlockRect(0));

    return hr;
}

HRESULT D3D9RenderTextureToScreen::DoRender(D3D9DeviceProvider *pDP)
{
    HRESULT hr = S_OK;

    IDirect3DDevice9 *pDevice = pDP->Device(0);

    HTEST(pDevice->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff7f7f7f, 1.0f, 0));

    HTEST(pDevice->BeginScene());

    HTEST(pDevice->SetStreamSource(0, mpVB, 0, sizeof(Vertex)));
    HTEST(pDevice->SetVertexDeclaration(mpVertexDecl));
    HTEST(pDevice->SetVertexShader(mpVS));
    HTEST(pDevice->SetPixelShader(mpPS));
    HTEST(pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
    HTEST(pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE));

    HTEST(pDevice->SetTexture(0, mpTex));
    HTEST(pDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR));
    HTEST(pDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR));
    HTEST(pDevice->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR));

    HTEST(pDevice->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2));

    HTEST(pDevice->EndScene());

    HTEST(pDevice->Present(0, 0, 0, 0));

    return S_OK;
}


/*
 * "Public" interface.
 */

D3D9Render *CreateRender(int iRenderId)
{
    switch (iRenderId)
    {
        case 11:
            return new D3D9RenderTextureToScreen(0.0f);
        case 10:
            return new D3D9RenderTextureToScreen(-0.5f);
        case 9:
            return new D3D9RenderTexture();
        case 8:
            return new D3D9RenderColorFill();
        case 7:
            return new D3D9RenderShared();
        case 6:
            return new D3D9RenderDepth();
        case 5:
            return new D3D9RenderInstance();
        case 4:
            return new D3D9RenderCubeMap();
        case 3:
            return new D3D9RenderTriangleShader();
        case 2:
            return new D3D9RenderTriangleFVF();
        case 1:
            return new D3D9RenderTriangle();
        case 0:
            return new D3D9RenderClear();
        default:
            break;
    }
    return 0;
}

void DeleteRender(D3D9Render *pRender)
{
    delete pRender;
}
