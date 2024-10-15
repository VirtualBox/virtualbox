/* $Id: GaDxva.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#include "../VBoxDispD3DCmn.h"

#define D3D_RELEASE(ptr) do { \
    if (ptr)                  \
    {                         \
        (ptr)->Release();     \
        (ptr) = 0;            \
    }                         \
} while (0)

struct Vertex
{
    float x, y; /* The vertex position in pixels. */
    float u, v; /* Normalized texture coordinates. */
};

/* Saved context. */
typedef struct VBOXDXVAD3D9SAVEDSTATE
{
    D3DVIEWPORT9            Viewport;
    DWORD                   rsCull;
    DWORD                   rsZEnable;
    IDirect3DSurface9      *pRT;
    IDirect3DVertexShader9 *pVS;
    IDirect3DPixelShader9  *pPS;
    IDirect3DBaseTexture9  *pTexture;
    float                   aVSConstantData[4];
    float                   aPSConstantData[4];
    DWORD                   ssMagFilter;
    DWORD                   ssMinFilter;
    DWORD                   ssMipFilter;
} VBOXDXVAD3D9SAVEDSTATE;

/*
 * Draw a quad in order to convert the input resource to the output render target.
 * The pixel shader will provide required conversion.
 */
typedef struct VBOXWDDMVIDEOPROCESSDEVICE
{
    /* Creation parameters. */
    PVBOXWDDMDISP_DEVICE   pDevice;
    GUID                   VideoProcGuid;
    DXVADDI_VIDEODESC      VideoDesc;
    D3DDDIFORMAT           RenderTargetFormat;
    UINT                   MaxSubStreams;

    /* The current render target, i.e. the Blt destination. */
    PVBOXWDDMDISP_RESOURCE pRenderTarget;
    UINT                   RTSubResourceIndex;
    IDirect3DTexture9      *pRTTexture;
    IDirect3DSurface9      *pRTSurface;

    /* Private objects for video processing. */
    IDirect3DTexture9           *pStagingTexture;    /* Intermediate texture. */
    IDirect3DVertexBuffer9      *pVB;         /* Vertex buffer which describes the quad we render. */
    IDirect3DVertexDeclaration9 *pVertexDecl; /* Vertex declaration for the quad vertices. */
    IDirect3DVertexShader9      *pVS;         /* Vertex shader. */
    IDirect3DPixelShader9       *pPS;         /* Pixel shader. */

    /* Saved D3D device state, which the blitter changes. */
    VBOXDXVAD3D9SAVEDSTATE SavedState;
} VBOXWDDMVIDEOPROCESSDEVICE;

static GUID const gaDeviceGuids[] =
{
    DXVADDI_VideoProcProgressiveDevice,
    DXVADDI_VideoProcBobDevice
};

static D3DDDIFORMAT const gaInputFormats[] =
{
    D3DDDIFMT_YUY2
};

static D3DDDIFORMAT const gaOutputFormats[] =
{
    D3DDDIFMT_A8R8G8B8,
    D3DDDIFMT_X8R8G8B8
};

static int vboxDxvaFindDeviceGuid(GUID const *pGuid)
{
    for (int i = 0; i < RT_ELEMENTS(gaDeviceGuids); ++i)
    {
        if (memcmp(pGuid, &gaDeviceGuids[i], sizeof(GUID)) == 0)
            return i;
    }
    return -1;
}

static int vboxDxvaFindInputFormat(D3DDDIFORMAT enmFormat)
{
    for (int i = 0; i < RT_ELEMENTS(gaInputFormats); ++i)
    {
        if (enmFormat == gaInputFormats[0])
            return i;
    }
    return -1;
}

static HRESULT vboxDxvaCopyToVertexBuffer(IDirect3DVertexBuffer9 *pVB, const void *pvSrc, int cbSrc)
{
    void *pvDst = 0;
    HRESULT hr = pVB->Lock(0, 0, &pvDst, 0);
    if (SUCCEEDED(hr))
    {
        memcpy(pvDst, pvSrc, cbSrc);
        hr = pVB->Unlock();
    }
    return hr;
}

static HRESULT vboxDxvaDeviceStateSave(IDirect3DDevice9 *pDevice9, VBOXDXVAD3D9SAVEDSTATE *pState)
{
    HRESULT hr;

    hr = pDevice9->GetViewport(&pState->Viewport);
    AssertReturn(hr == D3D_OK, hr);

    hr = pDevice9->GetRenderState(D3DRS_CULLMODE, &pState->rsCull);
    AssertReturn(hr == D3D_OK, hr);

    hr = pDevice9->GetRenderState(D3DRS_ZENABLE, &pState->rsZEnable);
    AssertReturn(hr == D3D_OK, hr);

    hr = pDevice9->GetRenderTarget(0, &pState->pRT);
    AssertReturn(hr == D3D_OK, hr);

    hr = pDevice9->GetVertexShader(&pState->pVS);
    AssertReturn(hr == D3D_OK, hr);

    hr = pDevice9->GetPixelShader(&pState->pPS);
    AssertReturn(hr == D3D_OK, hr);

    hr = pDevice9->GetTexture(0, &pState->pTexture);
    AssertReturn(hr == D3D_OK, hr);

    hr = pDevice9->GetVertexShaderConstantF(0, pState->aVSConstantData, 1);
    AssertReturn(hr == D3D_OK, hr);

    hr = pDevice9->GetPixelShaderConstantF(0, pState->aPSConstantData, 1);
    AssertReturn(hr == D3D_OK, hr);

    hr = pDevice9->GetSamplerState(0, D3DSAMP_MAGFILTER, &pState->ssMagFilter);
    AssertReturn(hr == D3D_OK, hr);

    hr = pDevice9->GetSamplerState(0, D3DSAMP_MINFILTER, &pState->ssMinFilter);
    AssertReturn(hr == D3D_OK, hr);

    hr = pDevice9->GetSamplerState(0, D3DSAMP_MIPFILTER, &pState->ssMipFilter);
    AssertReturn(hr == D3D_OK, hr);

    return hr;
}

static void vboxDxvaDeviceStateRestore(IDirect3DDevice9 *pDevice9, VBOXDXVAD3D9SAVEDSTATE const *pState)
{
    HRESULT hr;

    hr = pDevice9->SetViewport(&pState->Viewport);
    Assert(hr == D3D_OK);

    hr = pDevice9->SetRenderState(D3DRS_CULLMODE, pState->rsCull);
    Assert(hr == D3D_OK);

    hr = pDevice9->SetRenderState(D3DRS_ZENABLE, pState->rsZEnable);
    Assert(hr == D3D_OK);

    hr = pDevice9->SetRenderTarget(0, pState->pRT);
    Assert(hr == D3D_OK);

    hr = pDevice9->SetVertexShader(pState->pVS);
    Assert(hr == D3D_OK);

    hr = pDevice9->SetPixelShader(pState->pPS);
    Assert(hr == D3D_OK);

    hr = pDevice9->SetTexture(0, pState->pTexture);
    Assert(hr == D3D_OK);

    hr = pDevice9->SetVertexShaderConstantF(0, pState->aVSConstantData, 1);
    Assert(hr == D3D_OK);

    hr = pDevice9->SetPixelShaderConstantF(0, pState->aPSConstantData, 1);
    Assert(hr == D3D_OK);

    hr = pDevice9->SetSamplerState(0, D3DSAMP_MAGFILTER, pState->ssMagFilter);
    Assert(hr == D3D_OK);

    hr = pDevice9->SetSamplerState(0, D3DSAMP_MINFILTER, pState->ssMinFilter);
    Assert(hr == D3D_OK);

    hr = pDevice9->SetSamplerState(0, D3DSAMP_MIPFILTER, pState->ssMipFilter);
    Assert(hr == D3D_OK);
}

static HRESULT vboxDxvaUploadSample(VBOXWDDMVIDEOPROCESSDEVICE *pVideoProcessDevice,
                                    IDirect3DTexture9 *pSrcTexture,
                                    UINT SrcSubResourceIndex)
{
    HRESULT hr;

    /*
     * Upload the source data to the staging texture.
     */
    D3DLOCKED_RECT StagingLockedRect;
    hr = pVideoProcessDevice->pStagingTexture->LockRect(0, /* texture level */
                                                        &StagingLockedRect,
                                                        NULL,   /* entire texture */
                                                        D3DLOCK_DISCARD);
    Assert(hr == D3D_OK);
    if (hr == D3D_OK)
    {
        D3DLOCKED_RECT SampleLockedRect;
        hr = pSrcTexture->LockRect(SrcSubResourceIndex, /* texture level */
                                   &SampleLockedRect,
                                   NULL,   /* entire texture */
                                   D3DLOCK_READONLY);
        Assert(hr == D3D_OK);
        if (hr == D3D_OK)
        {
            uint8_t *pDst = (uint8_t *)StagingLockedRect.pBits;
            const uint8_t *pSrc = (uint8_t *)SampleLockedRect.pBits;
            for (uint32_t j = 0; j < pVideoProcessDevice->VideoDesc.SampleHeight; ++j)
            {
                memcpy(pDst, pSrc, RT_MIN(SampleLockedRect.Pitch, StagingLockedRect.Pitch));

                pDst += StagingLockedRect.Pitch;
                pSrc += SampleLockedRect.Pitch;
            }

            pSrcTexture->UnlockRect(SrcSubResourceIndex /* texture level */);
        }
        pVideoProcessDevice->pStagingTexture->UnlockRect(0 /* texture level */);
    }

    return hr;
}

/*
 * The shader code has been obtained from the hex listing file (hexdump.txt) produced by fxc HLSL compiler:
 * fxc.exe /Op /Tfx_2_0 /Fxhexdump.txt shader.fx
 *
    uniform extern float4 gTextureInfo; // .xy = (TargetWidth, TargetHeight), .zw = (SourceWidth, SourceHeight) in pixels
    uniform extern texture gTexSource;
    sampler sSource = sampler_state
    {
        Texture = <gTexSource>;
    };

    struct VS_INPUT
    {
        float2 Position   : POSITION;  // In pixels
        float2 TexCoord   : TEXCOORD0; // Normalized
    };

    struct VS_OUTPUT
    {
        float4 Position   : POSITION;  // Normalized
        float2 TexCoord   : TEXCOORD0; // Normalized
    };

    VS_OUTPUT VS(VS_INPUT In)
    {
        VS_OUTPUT Output;

        // Target position is in pixels, i.e left,top is 0,0; right,bottom is width - 1,height - 1.
        // Convert to the normalized coords in the -1;1 range (x right, y up).
        float4 Position;
        Position.x =  2.0f * In.Position.x / (gTextureInfo.x - 1.0f) - 1.0f;
        Position.y = -2.0f * In.Position.y / (gTextureInfo.y - 1.0f) + 1.0f;
        Position.z = 0.0f; // Not used.
        Position.w = 1.0f; // It is a point.

        Output.Position  = Position;
        Output.TexCoord  = In.TexCoord;

        return Output;
    }

    struct PS_OUTPUT
    {
        float4 Color : COLOR0;
    };

    static const float3x3 yuvCoeffs =
    {
        1.164383f,  1.164383f, 1.164383f,
        0.0f,      -0.391762f, 2.017232f,
        1.596027f, -0.812968f, 0.0f
    };

    PS_OUTPUT PS(VS_OUTPUT In)
    {
        PS_OUTPUT Output;

        // 4 bytes of an YUV macropixel contain 2 pixels in X for the target.
        // I.e. each YUV texture pixel is sampled twice: for both even and odd target pixels.

        // In.TexCoord are in [0;1] range for the source texture.
        float2 texCoord = In.TexCoord;

        // Source texture data is half width, i.e. it contains data in pixels [0; width / 2 - 1].
        texCoord.x = texCoord.x / 2.0f;

        // Which source pixel needs to be read: xPixel = TexCoord.x * SourceWidth.
        float xSourcePixel = texCoord.x * gTextureInfo.z;

        // Remainder is about 0.25 for even pixels and about 0.75 for odd pixels.
        float remainder = xSourcePixel - trunc(xSourcePixel);

        // Fetch YUV
        float4 texColor = tex2D(sSource, texCoord);

        // Get YUV components.
        float y0 = texColor.b;
        float u  = texColor.g;
        float y1 = texColor.r;
        float v  = texColor.a;

        // Get y0 for even x coordinates and y1 for odd ones.
        float y = remainder < 0.5f ? y0 : y1;

        // Make a vector for easier calculation.
        float3 yuv = float3(y, u, v);

        // Convert YUV to RGB for BT.601:
        // https://docs.microsoft.com/en-us/windows/win32/medfound/recommended-8-bit-yuv-formats-for-video-rendering#converting-8-bit-yuv-to-rgb888
        //
        // For 8bit [0;255] when Y = [16;235], U,V = [16;239]:
        //
        //   C = Y - 16
        //   D = U - 128
        //   E = V - 128
        //
        //   R = 1.164383 * C                + 1.596027 * E
        //   G = 1.164383 * C - 0.391762 * D - 0.812968 * E
        //   B = 1.164383 * C + 2.017232 * D
        //
        // For shader values [0;1.0] when Y = [16/255;235/255], U,V = [16/255;239/255]:
        //
        //   C = Y - 0.0627
        //   D = U - 0.5020
        //   E = V - 0.5020
        //
        //   R = 1.164383 * C                + 1.596027 * E
        //   G = 1.164383 * C - 0.391762 * D - 0.812968 * E
        //   B = 1.164383 * C + 2.017232 * D
        //
        yuv -= float3(0.0627f, 0.502f, 0.502f);
        float3 bgr = mul(yuv, yuvCoeffs);

        // Clamp to [0;1]
        bgr = saturate(bgr);

        // Return RGBA
        Output.Color = float4(bgr, 1.0f);

        return Output;
    }

    technique RenderScene
    {
        pass P0
        {
            VertexShader = compile vs_2_0 VS();
            PixelShader  = compile ps_2_0 PS();
        }
    }
 */

static DWORD const aVSCode[] =
{
    0xfffe0200,                                                             // vs_2_0
    0x05000051, 0xa00f0001, 0xbf800000, 0xc0000000, 0x3f800000, 0x00000000, // def c1, -1, -2, 1, 0
    0x0200001f, 0x80000000, 0x900f0000,                                     // dcl_position v0
    0x0200001f, 0x80000005, 0x900f0001,                                     // dcl_texcoord v1
    0x03000002, 0x80010000, 0x90000000, 0x90000000,                         // add r0.x, v0.x, v0.x
    0x02000001, 0x80010001, 0xa0000001,                                     // mov r1.x, c1.x
    0x03000002, 0x80060000, 0x80000001, 0xa0d00000,                         // add r0.yz, r1.x, c0.xxyw
    0x02000006, 0x80020000, 0x80550000,                                     // rcp r0.y, r0.y
    0x02000006, 0x80040000, 0x80aa0000,                                     // rcp r0.z, r0.z
    0x04000004, 0xc0010000, 0x80000000, 0x80550000, 0xa0000001,             // mad oPos.x, r0.x, r0.y, c1.x
    0x03000005, 0x80010000, 0x90550000, 0xa0550001,                         // mul r0.x, v0.y, c1.y
    0x04000004, 0xc0020000, 0x80000000, 0x80aa0000, 0xa0aa0001,             // mad oPos.y, r0.x, r0.z, c1.z
    0x02000001, 0xc00c0000, 0xa0b40001,                                     // mov oPos.zw, c1.xywz
    0x02000001, 0xe0030000, 0x90e40001,                                     // mov oT0.xy, v1
    0x0000ffff
};

static DWORD const aPSCodeYUY2toRGB[] =
{
    0xffff0200,                                                             // ps_2_0
    0x05000051, 0xa00f0005, 0x3f000000, 0x00000000, 0x3f800000, 0x3f000000, // def c5, 0.5, 0, 1, 0.5
    0x0200001f, 0x80000000, 0xb0030000,                                     // dcl t0.xy
    0x0200001f, 0x90000000, 0xa00f0800,                                     // dcl_2d s0
    0x03000005, 0x80080000, 0xb0000000, 0xa0000005,                         // mul r0.w, t0.x, c5.x
    0x03000005, 0x80010000, 0x80ff0000, 0xa0aa0000,                         // mul r0.x, r0.w, c0.z
    0x02000013, 0x80020000, 0x80000000,                                     // frc r0.y, r0.x
    0x04000058, 0x80040000, 0x81550000, 0xa0550005, 0xa0aa0005,             // cmp r0.z, -r0.y, c5.y, c5.z
    0x03000002, 0x80020000, 0x80000000, 0x81550000,                         // add r0.y, r0.x, -r0.y
    0x04000058, 0x80010000, 0x80000000, 0xa0550005, 0x80aa0000,             // cmp r0.x, r0.x, c5.y, r0.z
    0x03000002, 0x80010000, 0x80000000, 0x80550000,                         // add r0.x, r0.x, r0.y
    0x04000004, 0x80010000, 0x80ff0000, 0xa0aa0000, 0x81000000,             // mad r0.x, r0.w, c0.z, -r0.x
    0x03000002, 0x80010000, 0x80000000, 0xa1ff0005,                         // add r0.x, r0.x, -c5.w
    0x03000005, 0x80030001, 0xb0e40000, 0xa01b0005,                         // mul r1.xy, t0, c5.wzyx
    0x03000042, 0x800f0001, 0x80e40001, 0xa0e40800,                         // texld r1, r1, s0
    0x04000058, 0x80010001, 0x80000000, 0x80000001, 0x80aa0001,             // cmp r1.x, r0.x, r1.x, r1.z
    0x02000001, 0x80040001, 0x80ff0001,                                     // mov r1.z, r1.w
    0x03000002, 0x80070000, 0x80e40001, 0xa1e40001,                         // add r0.xyz, r1, -c1
    0x03000008, 0x80110001, 0x80e40000, 0xa0e40002,                         // dp3_sat r1.x, r0, c2
    0x03000008, 0x80120001, 0x80e40000, 0xa0e40003,                         // dp3_sat r1.y, r0, c3
    0x03000008, 0x80140001, 0x80e40000, 0xa0e40004,                         // dp3_sat r1.z, r0, c4
    0x02000001, 0x80080001, 0xa0aa0005,                                     // mov r1.w, c5.z
    0x02000001, 0x800f0800, 0x80e40001,                                     // mov oC0, r1
    0x0000ffff
};


static HRESULT vboxDxvaInit(VBOXWDDMVIDEOPROCESSDEVICE *pVideoProcessDevice)
{
    HRESULT hr;

    IDirect3DDevice9 *pDevice9 = pVideoProcessDevice->pDevice->pDevice9If;

    DWORD const *paVS = &aVSCode[0];
    DWORD const *paPS = &aPSCodeYUY2toRGB[0];

    static D3DVERTEXELEMENT9 const aVertexElements[] =
    {
        {0,  0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0,  8, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        D3DDECL_END()
    };

    hr = pDevice9->CreateVertexDeclaration(aVertexElements, &pVideoProcessDevice->pVertexDecl);
    AssertReturn(hr == D3D_OK, hr);

    hr = pDevice9->CreateVertexBuffer(6 * sizeof(Vertex), /* 2 triangles. */
                                      D3DUSAGE_WRITEONLY,
                                      0, /* FVF */
                                      D3DPOOL_DEFAULT,
                                      &pVideoProcessDevice->pVB,
                                      0);
    AssertReturn(hr == D3D_OK, hr);

    hr = pDevice9->CreateVertexShader(paVS, &pVideoProcessDevice->pVS);
    AssertReturn(hr == D3D_OK, hr);

    hr = pDevice9->CreatePixelShader(paPS, &pVideoProcessDevice->pPS);
    AssertReturn(hr == D3D_OK, hr);

    hr = pDevice9->CreateTexture(pVideoProcessDevice->VideoDesc.SampleWidth,
                                 pVideoProcessDevice->VideoDesc.SampleHeight,
                                 0, /* Levels */
                                 0, /* D3DUSAGE_ */
                                 D3DFMT_A8R8G8B8, //D3DFMT_YUY2,
                                 D3DPOOL_DEFAULT,
                                 &pVideoProcessDevice->pStagingTexture,
                                 NULL);
    AssertReturn(hr == D3D_OK, hr);

    return S_OK;
}

static float const aPSConstsBT601[] =
{
    0.062745f,  0.501961f,  0.501961f, 0.0f, // offsets
    // Y        U           V
    1.164384f,  0.000000f,  1.596027f, 0.0f, // R
    1.164384f, -0.391762f, -0.812968f, 0.0f, // G
    1.164384f,  2.017232f,  0.000000f, 0.0f, // B
};
static float const aPSConstsBT709[] =
{
    0.062745f,  0.501961f,  0.501961f, 0.0f, // offsets
    // Y        U           V
    1.164384f,  0.000000f,  1.792741f, 0.0f, // R
    1.164384f, -0.213249f, -0.532909f, 0.0f, // G
    1.164384f,  2.112402f,  0.000000f, 0.0f, // B
};
static float const aPSConstsSMPTE240M[] =
{
    0.062745f,  0.501961f,  0.501961f, 0.0f, // offsets
    // Y        U           V
    1.164384f,  0.000000f,  1.794107f, 0.0f, // R
    1.164384f, -0.257985f, -0.542583f, 0.0f, // G
    1.164384f,  2.078705f,  0.000000f, 0.0f, // B
};

static HRESULT vboxDxvaSetState(VBOXWDDMVIDEOPROCESSDEVICE *pVideoProcessDevice, DXVADDI_EXTENDEDFORMAT const *pSampleFormat)
{
    HRESULT hr;

    IDirect3DDevice9 *pDevice9 = pVideoProcessDevice->pDevice->pDevice9If;

    hr = pDevice9->SetStreamSource(0, pVideoProcessDevice->pVB, 0, sizeof(Vertex));
    AssertReturn(hr == D3D_OK, hr);

    hr = pDevice9->SetVertexDeclaration(pVideoProcessDevice->pVertexDecl);
    AssertReturn(hr == D3D_OK, hr);

    hr = pDevice9->SetVertexShader(pVideoProcessDevice->pVS);
    AssertReturn(hr == D3D_OK, hr);

    hr = pDevice9->SetPixelShader(pVideoProcessDevice->pPS);
    AssertReturn(hr == D3D_OK, hr);

    hr = pDevice9->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    AssertReturn(hr == D3D_OK, hr);

    hr = pDevice9->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    AssertReturn(hr == D3D_OK, hr);

    hr = pDevice9->SetTexture(0, pVideoProcessDevice->pStagingTexture);
    AssertReturn(hr == D3D_OK, hr);

    float const cTargetWidth = pVideoProcessDevice->pRenderTarget->aAllocations[0].SurfDesc.width;
    float const cTargetHeight = pVideoProcessDevice->pRenderTarget->aAllocations[0].SurfDesc.height;

    float const cSampleWidth = pVideoProcessDevice->VideoDesc.SampleWidth;
    float const cSampleHeight = pVideoProcessDevice->VideoDesc.SampleHeight;

    float aTextureInfo[4];
    aTextureInfo[0] = cTargetWidth;
    aTextureInfo[1] = cTargetHeight;
    aTextureInfo[2] = cSampleWidth;
    aTextureInfo[3] = cSampleHeight;

    hr = pDevice9->SetVertexShaderConstantF(0, aTextureInfo, 1);
    AssertReturn(hr == D3D_OK, hr);
    hr = pDevice9->SetPixelShaderConstantF(0, aTextureInfo, 1);
    AssertReturn(hr == D3D_OK, hr);

    float const *pConstantData;
    UINT Vector4fCount;
    switch (pSampleFormat->VideoTransferMatrix)
    {
        case DXVADDI_VideoTransferMatrix_BT709:
            pConstantData = aPSConstsBT709;
            Vector4fCount = RT_ELEMENTS(aPSConstsBT709) / 4;
            break;
        case DXVADDI_VideoTransferMatrix_SMPTE240M:
            pConstantData = aPSConstsSMPTE240M;
            Vector4fCount = RT_ELEMENTS(aPSConstsSMPTE240M) / 4;
            break;
        case DXVADDI_VideoTransferMatrix_BT601:
        default:
            pConstantData = aPSConstsBT601;
            Vector4fCount = RT_ELEMENTS(aPSConstsBT601) / 4;
            break;
    }

    hr = pDevice9->SetPixelShaderConstantF(1, pConstantData, Vector4fCount);
    AssertReturn(hr == D3D_OK, hr);

    hr = pDevice9->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    AssertReturn(hr == D3D_OK, hr);
    hr = pDevice9->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    AssertReturn(hr == D3D_OK, hr);
    hr = pDevice9->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    AssertReturn(hr == D3D_OK, hr);

    hr = pDevice9->SetRenderTarget(0, pVideoProcessDevice->pRTSurface);
    AssertReturn(hr == D3D_OK, hr);

    return S_OK;
}

static HRESULT vboxDxvaUpdateVertexBuffer(VBOXWDDMVIDEOPROCESSDEVICE *pVideoProcessDevice,
                                          RECT const *pSrcRect,
                                          RECT const *pDstRect)
{
    HRESULT hr;

    /* Do not display anything if the source rectangle is not what is expected.
     * But assert anyway in order to be able to investigate.
     */
    AssertReturn(pSrcRect->right > pSrcRect->left, S_OK);
    AssertReturn(pSrcRect->bottom > pSrcRect->top, S_OK);

    float const cSrcWidth  = pVideoProcessDevice->VideoDesc.SampleWidth;
    float const cSrcHeight = pVideoProcessDevice->VideoDesc.SampleHeight;

    float const uSrcLeft   = (float)pSrcRect->left   / cSrcWidth;
    float const uSrcRight  = (float)pSrcRect->right  / cSrcWidth;
    float const vSrcTop    = (float)pSrcRect->top    / cSrcHeight;
    float const vSrcBottom = (float)pSrcRect->bottom / cSrcHeight;

    /* Subtract 0.5 to line up the pixel centers with texels
     * https://docs.microsoft.com/en-us/windows/win32/direct3d9/directly-mapping-texels-to-pixels
     */
    float const xDstLeft   = (float)pDstRect->left   - 0.5f;
    float const xDstRight  = (float)pDstRect->right  - 0.5f;
    float const yDstTop    = (float)pDstRect->top    - 0.5f;
    float const yDstBottom = (float)pDstRect->bottom - 0.5f;

    Vertex const aVertices[] =
    {
        { xDstLeft,  yDstTop,    uSrcLeft,  vSrcTop},
        { xDstRight, yDstTop,    uSrcRight, vSrcTop},
        { xDstRight, yDstBottom, uSrcRight, vSrcBottom},

        { xDstLeft,  yDstTop,    uSrcLeft,  vSrcTop},
        { xDstRight, yDstBottom, uSrcRight, vSrcBottom},
        { xDstLeft,  yDstBottom, uSrcLeft,  vSrcBottom},
    };

    hr = vboxDxvaCopyToVertexBuffer(pVideoProcessDevice->pVB, aVertices, sizeof(aVertices));
    AssertReturn(hr == D3D_OK, hr);

    return S_OK;
}

static HRESULT vboxDxvaProcessBlt(VBOXWDDMVIDEOPROCESSDEVICE *pVideoProcessDevice,
                                  D3DDDIARG_VIDEOPROCESSBLT const *pData,
                                  IDirect3DTexture9 *paSrcTextures[])
{
    HRESULT hr;

    IDirect3DDevice9 *pDevice9 = pVideoProcessDevice->pDevice->pDevice9If;

    hr = vboxDxvaDeviceStateSave(pDevice9, &pVideoProcessDevice->SavedState);
    if (hr == D3D_OK)
    {
        /* Set the required state for the blits, inclusding the render target. */
        hr = vboxDxvaSetState(pVideoProcessDevice, &pData->pSrcSurfaces[0].SampleFormat);
        if (hr == D3D_OK)
        {
            /* Clear the target rectangle. */
            /** @todo Use pData->BackgroundColor */
            D3DCOLOR BgColor = 0;
            D3DRECT TargetRect;
            TargetRect.x1 = pData->TargetRect.left;
            TargetRect.y1 = pData->TargetRect.top;
            TargetRect.x2 = pData->TargetRect.right;
            TargetRect.y2 = pData->TargetRect.bottom;
            hr = pDevice9->Clear(1, &TargetRect, D3DCLEAR_TARGET, BgColor, 0.0f, 0);
            Assert(hr == D3D_OK); /* Ignore errors. */

            DXVADDI_VIDEOSAMPLE const *pSrcSample = &pData->pSrcSurfaces[0];
            IDirect3DTexture9 *pSrcTexture = paSrcTextures[0];

            /* Upload the source data to the staging texture. */
            hr = vboxDxvaUploadSample(pVideoProcessDevice, pSrcTexture, pSrcSample->SrcSubResourceIndex);
            if (hr == D3D_OK)
            {
                /* Setup the blit dimensions. */
                hr = vboxDxvaUpdateVertexBuffer(pVideoProcessDevice, &pSrcSample->SrcRect, &pSrcSample->DstRect);
                Assert(hr == D3D_OK);
                if (hr == D3D_OK)
                {

                    hr = pDevice9->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2);
                    Assert(hr == D3D_OK);
                }
            }
        }

        vboxDxvaDeviceStateRestore(pDevice9, &pVideoProcessDevice->SavedState);
    }

    return hr;
}

/*
 *
 * Public API.
 *
 */
HRESULT VBoxDxvaGetDeviceGuidCount(UINT *pcGuids)
{
    *pcGuids = RT_ELEMENTS(gaDeviceGuids);
    return S_OK;
}

HRESULT VBoxDxvaGetDeviceGuids(GUID *paGuids, UINT cbGuids)
{
    if (cbGuids >= sizeof(gaDeviceGuids))
    {
        memcpy(paGuids, &gaDeviceGuids[0], sizeof(gaDeviceGuids));
        return S_OK;
    }

    AssertFailed();
    return E_INVALIDARG;
}

HRESULT VBoxDxvaGetOutputFormatCount(UINT *pcFormats, DXVADDI_VIDEOPROCESSORINPUT const *pVPI, bool fSubstream)
{
    RT_NOREF(fSubstream);

    UINT cFormats = 0;
    if (pVPI)
    {
        if (vboxDxvaFindDeviceGuid(pVPI->pVideoProcGuid) >= 0)
        {
            if (vboxDxvaFindInputFormat(pVPI->VideoDesc.Format) >= 0)
            {
                cFormats = RT_ELEMENTS(gaOutputFormats);
            }
        }
    }

    *pcFormats = cFormats;
    return S_OK;
}

HRESULT VBoxDxvaGetOutputFormats(D3DDDIFORMAT *paFormats, UINT cbFormats, DXVADDI_VIDEOPROCESSORINPUT const *pVPI, bool fSubstream)
{
    RT_NOREF(fSubstream);

    if (pVPI)
    {
        if (vboxDxvaFindDeviceGuid(pVPI->pVideoProcGuid) >= 0)
        {
            if (vboxDxvaFindInputFormat(pVPI->VideoDesc.Format) >= 0)
            {
                if (cbFormats >= sizeof(gaOutputFormats))
                {
                    memcpy(paFormats, gaOutputFormats, sizeof(gaOutputFormats));
                    return S_OK;
                }
            }
        }
    }

    AssertFailed();
    return E_INVALIDARG;
}

HRESULT VBoxDxvaGetCaps(DXVADDI_VIDEOPROCESSORCAPS *pVideoProcessorCaps,
                        DXVADDI_VIDEOPROCESSORINPUT const *pVPI)
{
    RT_ZERO(*pVideoProcessorCaps);

    if (pVPI)
    {
        if (vboxDxvaFindDeviceGuid(pVPI->pVideoProcGuid) >= 0)
        {
            if (vboxDxvaFindInputFormat(pVPI->VideoDesc.Format) >= 0)
            {
                pVideoProcessorCaps->InputPool                = D3DDDIPOOL_SYSTEMMEM;
                pVideoProcessorCaps->NumForwardRefSamples     = 0; /// @todo 1 for deinterlacing?
                pVideoProcessorCaps->NumBackwardRefSamples    = 0;
                pVideoProcessorCaps->OutputFormat             = D3DDDIFMT_X8R8G8B8;
                pVideoProcessorCaps->DeinterlaceTechnology    = DXVADDI_DEINTERLACETECH_UNKNOWN;
                pVideoProcessorCaps->ProcAmpControlCaps       = DXVADDI_PROCAMP_NONE; /// @todo Maybe some?
                pVideoProcessorCaps->VideoProcessorOperations = DXVADDI_VIDEOPROCESS_YUV2RGB
                                                              | DXVADDI_VIDEOPROCESS_STRETCHX
                                                              | DXVADDI_VIDEOPROCESS_STRETCHY
                                                              | DXVADDI_VIDEOPROCESS_YUV2RGBEXTENDED
                                                              | DXVADDI_VIDEOPROCESS_CONSTRICTION
                                                              | DXVADDI_VIDEOPROCESS_LINEARSCALING
                                                              | DXVADDI_VIDEOPROCESS_GAMMACOMPENSATED;
                pVideoProcessorCaps->NoiseFilterTechnology    = DXVADDI_NOISEFILTERTECH_UNSUPPORTED;
                pVideoProcessorCaps->DetailFilterTechnology   = DXVADDI_DETAILFILTERTECH_UNSUPPORTED;
                return S_OK;
            }
        }
    }

    AssertFailed();
    return E_INVALIDARG;
}

HRESULT VBoxDxvaCreateVideoProcessDevice(PVBOXWDDMDISP_DEVICE pDevice, D3DDDIARG_CREATEVIDEOPROCESSDEVICE *pData)
{
    /*
     * Do minimum here. Devices are created and destroyed without being used.
     */
    VBOXWDDMVIDEOPROCESSDEVICE *pVideoProcessDevice =
        (VBOXWDDMVIDEOPROCESSDEVICE *)RTMemAllocZ(sizeof(VBOXWDDMVIDEOPROCESSDEVICE));
    if (pVideoProcessDevice)
    {
        pVideoProcessDevice->pDevice            = pDevice;
        pVideoProcessDevice->VideoProcGuid      = *pData->pVideoProcGuid;
        pVideoProcessDevice->VideoDesc          = pData->VideoDesc;
        pVideoProcessDevice->RenderTargetFormat = pData->RenderTargetFormat;
        pVideoProcessDevice->MaxSubStreams      = pData->MaxSubStreams;

        pData->hVideoProcess = pVideoProcessDevice;
        return S_OK;
    }

    return E_OUTOFMEMORY;
}

HRESULT VBoxDxvaDestroyVideoProcessDevice(PVBOXWDDMDISP_DEVICE pDevice, HANDLE hVideoProcessor)
{
    VBOXWDDMVIDEOPROCESSDEVICE *pVideoProcessDevice = (VBOXWDDMVIDEOPROCESSDEVICE *)hVideoProcessor;
    AssertReturn(pDevice == pVideoProcessDevice->pDevice, E_INVALIDARG);

    D3D_RELEASE(pVideoProcessDevice->pRTSurface);

    D3D_RELEASE(pVideoProcessDevice->pStagingTexture);
    D3D_RELEASE(pVideoProcessDevice->pVertexDecl);
    D3D_RELEASE(pVideoProcessDevice->pVB);
    D3D_RELEASE(pVideoProcessDevice->pVS);
    D3D_RELEASE(pVideoProcessDevice->pPS);

    RTMemFree(pVideoProcessDevice);

    return S_OK;
}

HRESULT VBoxDxvaVideoProcessBeginFrame(PVBOXWDDMDISP_DEVICE pDevice, HANDLE hVideoProcessor)
{
    VBOXWDDMVIDEOPROCESSDEVICE *pVideoProcessDevice = (VBOXWDDMVIDEOPROCESSDEVICE *)hVideoProcessor;
    AssertReturn(pDevice == pVideoProcessDevice->pDevice, E_INVALIDARG);
    AssertPtrReturn(pDevice->pDevice9If, E_INVALIDARG);

    HRESULT hr = S_OK;
    if (!pVideoProcessDevice->pStagingTexture)
    {
        hr = vboxDxvaInit(pVideoProcessDevice);
    }

    return hr;
}

HRESULT VBoxDxvaVideoProcessEndFrame(PVBOXWDDMDISP_DEVICE pDevice, D3DDDIARG_VIDEOPROCESSENDFRAME *pData)
{
    VBOXWDDMVIDEOPROCESSDEVICE *pVideoProcessDevice = (VBOXWDDMVIDEOPROCESSDEVICE *)pData->hVideoProcess;
    AssertReturn(pDevice == pVideoProcessDevice->pDevice, E_INVALIDARG);
    return S_OK;
}

HRESULT VBoxDxvaSetVideoProcessRenderTarget(PVBOXWDDMDISP_DEVICE pDevice,
                                            const D3DDDIARG_SETVIDEOPROCESSRENDERTARGET *pData)
{
    VBOXWDDMVIDEOPROCESSDEVICE *pVideoProcessDevice = (VBOXWDDMVIDEOPROCESSDEVICE *)pData->hVideoProcess;
    AssertReturn(pDevice == pVideoProcessDevice->pDevice, E_INVALIDARG);

    D3D_RELEASE(pVideoProcessDevice->pRTSurface);
    pVideoProcessDevice->pRenderTarget = NULL;
    pVideoProcessDevice->RTSubResourceIndex = 0;
    pVideoProcessDevice->pRTTexture = NULL;

    PVBOXWDDMDISP_RESOURCE pRc = (PVBOXWDDMDISP_RESOURCE)pData->hRenderTarget;
    AssertReturn(pRc->cAllocations > pData->SubResourceIndex, E_INVALIDARG);

    VBOXWDDMDISP_ALLOCATION *pAllocation = &pRc->aAllocations[pData->SubResourceIndex];
    AssertPtrReturn(pAllocation->pD3DIf, E_INVALIDARG);
    AssertReturn(pAllocation->enmD3DIfType == VBOXDISP_D3DIFTYPE_TEXTURE, E_INVALIDARG);

#ifdef LOG_ENABLED
    LOGREL_EXACT(("VideoProcess RT %dx%d sid=%u\n",
        pRc->aAllocations[0].SurfDesc.width, pRc->aAllocations[0].SurfDesc.height, pAllocation->hostID));
#endif

    IDirect3DTexture9 *pRTTexture = (IDirect3DTexture9 *)pAllocation->pD3DIf;
    HRESULT hr = pRTTexture->GetSurfaceLevel(pData->SubResourceIndex, &pVideoProcessDevice->pRTSurface);
    AssertReturn(hr == D3D_OK, E_INVALIDARG);

    pVideoProcessDevice->pRenderTarget = pRc;
    pVideoProcessDevice->RTSubResourceIndex = pData->SubResourceIndex;
    pVideoProcessDevice->pRTTexture = pRTTexture;

    return S_OK;
}

HRESULT VBoxDxvaVideoProcessBlt(PVBOXWDDMDISP_DEVICE pDevice, const D3DDDIARG_VIDEOPROCESSBLT *pData)
{
    VBOXWDDMVIDEOPROCESSDEVICE *pVideoProcessDevice = (VBOXWDDMVIDEOPROCESSDEVICE *)pData->hVideoProcess;
    AssertReturn(pDevice == pVideoProcessDevice->pDevice, E_INVALIDARG);
    AssertPtrReturn(pDevice->pDevice9If, E_INVALIDARG);
    AssertPtrReturn(pVideoProcessDevice->pRTSurface, E_INVALIDARG);

    AssertReturn(pData->NumSrcSurfaces > 0, E_INVALIDARG);

    PVBOXWDDMDISP_RESOURCE pSrcRc = (PVBOXWDDMDISP_RESOURCE)pData->pSrcSurfaces[0].SrcResource;
    AssertReturn(pSrcRc->cAllocations > pData->pSrcSurfaces[0].SrcSubResourceIndex, E_INVALIDARG);

    VBOXWDDMDISP_ALLOCATION *pAllocation = &pSrcRc->aAllocations[pData->pSrcSurfaces[0].SrcSubResourceIndex];
    AssertPtrReturn(pAllocation->pD3DIf, E_INVALIDARG);
    AssertReturn(pAllocation->enmD3DIfType == VBOXDISP_D3DIFTYPE_TEXTURE, E_INVALIDARG);

    IDirect3DTexture9 *pSrcTexture = (IDirect3DTexture9 *)pAllocation->pD3DIf;

#ifdef LOG_ENABLED
    LOGREL_EXACT(("VideoProcess Blt sid = %u fmt 0x%08x %d,%d %dx%d (%dx%d) -> %d,%d %dx%d (%d,%d %dx%d, %dx%d)\n",
        pAllocation->hostID, pSrcRc->aAllocations[0].SurfDesc.format,
        pData->pSrcSurfaces[0].SrcRect.left, pData->pSrcSurfaces[0].SrcRect.top,
        pData->pSrcSurfaces[0].SrcRect.right - pData->pSrcSurfaces[0].SrcRect.left,
        pData->pSrcSurfaces[0].SrcRect.bottom - pData->pSrcSurfaces[0].SrcRect.top,

        pSrcRc->aAllocations[0].SurfDesc.width,
        pSrcRc->aAllocations[0].SurfDesc.height,

        pData->pSrcSurfaces[0].DstRect.left, pData->pSrcSurfaces[0].DstRect.top,
        pData->pSrcSurfaces[0].DstRect.right - pData->pSrcSurfaces[0].DstRect.left,
        pData->pSrcSurfaces[0].DstRect.bottom - pData->pSrcSurfaces[0].DstRect.top,

        pData->TargetRect.left, pData->TargetRect.top,
        pData->TargetRect.right - pData->TargetRect.left, pData->TargetRect.bottom - pData->TargetRect.top,

        pVideoProcessDevice->pRenderTarget->aAllocations[0].SurfDesc.width,
        pVideoProcessDevice->pRenderTarget->aAllocations[0].SurfDesc.height));

    DXVADDI_EXTENDEDFORMAT *pSampleFormat = &pData->pSrcSurfaces[0].SampleFormat;
    LOGREL_EXACT(("VideoProcess Blt SampleFormat %u, VideoChromaSubsampling %u, NominalRange %u, VideoTransferMatrix %u, VideoLighting %u, VideoPrimaries %u, VideoTransferFunction %u\n",
        pSampleFormat->SampleFormat, pSampleFormat->VideoChromaSubsampling, pSampleFormat->NominalRange,
        pSampleFormat->VideoTransferMatrix, pSampleFormat->VideoLighting, pSampleFormat->VideoPrimaries,
        pSampleFormat->VideoTransferFunction));
#endif

    HRESULT hr = vboxDxvaProcessBlt(pVideoProcessDevice, pData, &pSrcTexture);
    return hr;
}
