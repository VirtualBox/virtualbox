/* $Id: DevVGA-SVGA3d-win-d3d9.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevVMWare - VMWare SVGA device Direct3D 9 backend.
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

#define LOG_GROUP LOG_GROUP_DEV_VMSVGA
#include <VBox/vmm/pdmdev.h>

#include <iprt/assert.h>

#include "DevVGA-SVGA.h"
#include "DevVGA-SVGA3d-internal.h"


typedef enum D3D9TextureType
{
    D3D9TextureType_Texture,
    D3D9TextureType_Bounce,
    D3D9TextureType_Emulated
} D3D9TextureType;

DECLINLINE(D3DCUBEMAP_FACES) vmsvga3dCubemapFaceFromIndex(uint32_t iFace)
{
    D3DCUBEMAP_FACES Face;
    switch (iFace)
    {
        case 0: Face = D3DCUBEMAP_FACE_POSITIVE_X; break;
        case 1: Face = D3DCUBEMAP_FACE_NEGATIVE_X; break;
        case 2: Face = D3DCUBEMAP_FACE_POSITIVE_Y; break;
        case 3: Face = D3DCUBEMAP_FACE_NEGATIVE_Y; break;
        case 4: Face = D3DCUBEMAP_FACE_POSITIVE_Z; break;
        default:
        case 5: Face = D3DCUBEMAP_FACE_NEGATIVE_Z; break;
    }
    return Face;
}

IDirect3DTexture9 *D3D9GetTexture(PVMSVGA3DSURFACE pSurface,
                                  D3D9TextureType enmType)
{
    IDirect3DTexture9 *p;
    switch (enmType)
    {
        default: AssertFailed();
            RT_FALL_THRU();
        case D3D9TextureType_Texture: p = pSurface->u.pTexture; break;
        case D3D9TextureType_Bounce: p = pSurface->bounce.pTexture; break;
        case D3D9TextureType_Emulated: p = pSurface->emulated.pTexture; break;
    }
    return p;
}

IDirect3DCubeTexture9 *D3D9GetCubeTexture(PVMSVGA3DSURFACE pSurface,
                                          D3D9TextureType enmType)
{
    IDirect3DCubeTexture9 *p;
    switch (enmType)
    {
        default: AssertFailed();
            RT_FALL_THRU();
        case D3D9TextureType_Texture: p = pSurface->u.pCubeTexture; break;
        case D3D9TextureType_Bounce: p = pSurface->bounce.pCubeTexture; break;
        case D3D9TextureType_Emulated: p = pSurface->emulated.pCubeTexture; break;
    }
    return p;
}

IDirect3DVolumeTexture9 *D3D9GetVolumeTexture(PVMSVGA3DSURFACE pSurface,
                                              D3D9TextureType enmType)
{
    IDirect3DVolumeTexture9 *p;
    switch (enmType)
    {
        default: AssertFailed();
            RT_FALL_THRU();
        case D3D9TextureType_Texture: p = pSurface->u.pVolumeTexture; break;
        case D3D9TextureType_Bounce: p = pSurface->bounce.pVolumeTexture; break;
        case D3D9TextureType_Emulated: p = pSurface->emulated.pVolumeTexture; break;
    }
    return p;
}

HRESULT D3D9GetTextureLevel(PVMSVGA3DSURFACE pSurface,
                            D3D9TextureType enmType,
                            uint32_t uFace,
                            uint32_t uMipmap,
                            IDirect3DSurface9 **ppD3DSurface)
{
    HRESULT hr;

    if (pSurface->enmD3DResType == VMSVGA3D_D3DRESTYPE_CUBE_TEXTURE)
    {
        Assert(pSurface->cFaces == 6);

        IDirect3DCubeTexture9 *p = D3D9GetCubeTexture(pSurface, enmType);
        if (p)
        {
            D3DCUBEMAP_FACES const FaceType = vmsvga3dCubemapFaceFromIndex(uFace);
            hr = p->GetCubeMapSurface(FaceType, uMipmap, ppD3DSurface);
            AssertMsg(hr == D3D_OK, ("GetCubeMapSurface failed with %x\n", hr));
        }
        else
        {
            AssertFailed();
            hr = E_INVALIDARG;
        }
    }
    else if (pSurface->enmD3DResType == VMSVGA3D_D3DRESTYPE_TEXTURE)
    {
        Assert(pSurface->cFaces == 1);
        Assert(uFace == 0);

        IDirect3DTexture9 *p = D3D9GetTexture(pSurface, enmType);
        if (p)
        {
            hr = p->GetSurfaceLevel(uMipmap, ppD3DSurface);
            AssertMsg(hr == D3D_OK, ("GetSurfaceLevel failed with %x\n", hr));
        }
        else
        {
            AssertFailed();
            hr = E_INVALIDARG;
        }
    }
    else
    {
        AssertMsgFailed(("No surface level for type %d\n", pSurface->enmD3DResType));
        hr = E_INVALIDARG;
    }

    return hr;
}


static HRESULT d3dCopyToVertexBuffer(IDirect3DVertexBuffer9 *pVB, const void *pvSrc, int cbSrc)
{
    HRESULT hr = D3D_OK;
    void *pvDst = 0;
    hr = pVB->Lock(0, 0, &pvDst, 0);
    if (SUCCEEDED(hr))
    {
        memcpy(pvDst, pvSrc, cbSrc);
        hr = pVB->Unlock();
    }
    return hr;
}

struct Vertex
{
    float x, y; /* The vertex position. */
    float u, v; /* Texture coordinates. */
};

typedef struct D3D9ConversionParameters
{
    DWORD const *paVS; /* Vertex shader code. */
    DWORD const *paPS; /* Pixel shader code. */
} D3D9ConversionParameters;

/** Select conversion shaders.
 * @param d3dfmtFrom Source texture format.
 * @param d3dfmtTo Target texture format.
 * @param pResult Where the tore pointers to the shader code.
 */
static HRESULT d3d9SelectConversion(D3DFORMAT d3dfmtFrom, D3DFORMAT d3dfmtTo,
                                    D3D9ConversionParameters *pResult)
{
    /*
     * The shader code has been obtained from the hex listing file (hexdump.txt) produced by fxc HLSL compiler:
     * fxc.exe /Op /Tfx_2_0 /Fxhexdump.txt shader.fx
     *
     * The vertex shader code is the same for all convestion variants.
     *
     * For example here is the complete code for aPSCodeSwapRB:

        uniform extern float4 gTextureInfo; // .xy = (width, height) in pixels, .zw = (1/width, 1/height)
        uniform extern texture gTexSource;
        sampler sSource = sampler_state
        {
            Texture = <gTexSource>;
        };

        struct VS_INPUT
        {
            float2 Position   : POSITION; // In pixels.
            float2 TexCoord   : TEXCOORD0;
        };

        struct VS_OUTPUT
        {
            float4 Position   : POSITION; // Normalized.
            float2 TexCoord   : TEXCOORD0;
        };

        VS_OUTPUT VS(VS_INPUT In)
        {
            VS_OUTPUT Output;

            // Position is in pixels, i.e [0; width - 1]. Top, left is 0,0.
            // Convert to the normalized coords in the -1;1 range
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

        PS_OUTPUT PS(VS_OUTPUT In)
        {
            PS_OUTPUT Output;

            float2 texCoord = In.TexCoord;

            float4 texColor = tex2D(sSource, texCoord);

            Output.Color = texColor.bgra; // Swizzle rgba -> bgra

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

    /*
     * Swap R and B components. Converts D3DFMT_A8R8G8B8 <-> D3DFMT_A8B8G8R8.
     */
    static DWORD const aPSCodeSwapRB[] =
    {
        0xffff0200,                                                             // ps_2_0
        0x0200001f, 0x80000000, 0xb0030000,                                     // dcl t0.xy
        0x0200001f, 0x90000000, 0xa00f0800,                                     // dcl_2d s0
        0x03000042, 0x800f0000, 0xb0e40000, 0xa0e40800,                         // texld r0, t0, s0
        0x02000001, 0x80090001, 0x80d20000,                                     // mov r1.xw, r0.zxyw
        0x02000001, 0x80040001, 0x80000000,                                     // mov r1.z, r0.x
        0x02000001, 0x80020001, 0x80550000,                                     // mov r1.y, r0.y
        0x02000001, 0x800f0800, 0x80e40001,                                     // mov oC0, r1
        0x0000ffff
    };

    /* YUY2 to RGB:

        // YUY2 if not defined
        // #define UYVY

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

            // In.TexCoord are in [0;1] range for the target.
            float2 texCoord = In.TexCoord;

            // Convert to the target coords in pixels: xPixel = TexCoord.x * Width.
            float xTargetPixel = texCoord.x * gTextureInfo.x;

            // Source texture is half width, i.e. it contains data in pixels [0; width / 2 - 1].
            float xSourcePixel = xTargetPixel / 2.0f;

            // Remainder is about 0.25 for even pixels and about 0.75 for odd pixels.
            float remainder = xSourcePixel - trunc(xSourcePixel);

            // Back to the normalized coords: texCoord.x = xPixel / Width.
            texCoord.x = xSourcePixel * gTextureInfo.z;

            // Fetch YUV
            float4 texColor = tex2D(sSource, texCoord);

            // Get YUV components.
        #ifdef UYVY
            float u  = texColor.b;
            float y0 = texColor.g;
            float v  = texColor.r;
            float y1 = texColor.a;
        #else // YUY2
            float y0 = texColor.b;
            float u  = texColor.g;
            float y1 = texColor.r;
            float v  = texColor.a;
        #endif

            // Get y0 for even x coordinates and y1 for odd ones.
            float y = remainder < 0.5f ? y0 : y1;

            // Make a vector for easier calculation.
            float3 yuv = float3(y, u, v);

            // Convert YUV to RGB:
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
    */
    static DWORD const aPSCodeYUY2toRGB[] =
    {
        0xffff0200,                                                             // ps_2_0
        0x05000051, 0xa00f0001, 0x3f000000, 0x00000000, 0x3f800000, 0xbf000000, // def c1, 0.5, 0, 1, -0.5
        0x05000051, 0xa00f0002, 0xbd8068dc, 0xbf008312, 0xbf008312, 0x00000000, // def c2, -0.0627000034, -0.501999974, -0.501999974, 0
        0x05000051, 0xa00f0003, 0x3f950a81, 0x00000000, 0x3fcc4a9d, 0x00000000, // def c3, 1.16438305, 0, 1.59602702, 0
        0x05000051, 0xa00f0004, 0x3f950a81, 0xbec89507, 0xbf501eac, 0x00000000, // def c4, 1.16438305, -0.391761988, -0.812968016, 0
        0x05000051, 0xa00f0005, 0x3f950a81, 0x40011a54, 0x00000000, 0x00000000, // def c5, 1.16438305, 2.01723194, 0, 0
        0x0200001f, 0x80000000, 0xb0030000,                                     // dcl t0.xy
        0x0200001f, 0x90000000, 0xa00f0800,                                     // dcl_2d s0
        0x03000005, 0x80080000, 0xb0000000, 0xa0000000,                         // mul r0.w, t0.x, c0.x
        0x03000005, 0x80010000, 0x80ff0000, 0xa0000001,                         // mul r0.x, r0.w, c1.x
        0x02000013, 0x80020000, 0x80000000,                                     // frc r0.y, r0.x
        0x04000058, 0x80040000, 0x81550000, 0xa0550001, 0xa0aa0001,             // cmp r0.z, -r0.y, c1.y, c1.z
        0x03000002, 0x80020000, 0x80000000, 0x81550000,                         // add r0.y, r0.x, -r0.y
        0x03000005, 0x80010001, 0x80000000, 0xa0aa0000,                         // mul r1.x, r0.x, c0.z
        0x04000058, 0x80010000, 0x80ff0000, 0xa0550001, 0x80aa0000,             // cmp r0.x, r0.w, c1.y, r0.z
        0x03000002, 0x80010000, 0x80000000, 0x80550000,                         // add r0.x, r0.x, r0.y
        0x04000004, 0x80010000, 0x80ff0000, 0xa0000001, 0x81000000,             // mad r0.x, r0.w, c1.x, -r0.x
        0x03000002, 0x80010000, 0x80000000, 0xa0ff0001,                         // add r0.x, r0.x, c1.w
        0x02000001, 0x80020001, 0xb0550000,                                     // mov r1.y, t0.y
        0x03000042, 0x800f0001, 0x80e40001, 0xa0e40800,                         // texld r1, r1, s0
        0x04000058, 0x80010001, 0x80000000, 0x80000001, 0x80aa0001,             // cmp r1.x, r0.x, r1.x, r1.z
        0x02000001, 0x80040001, 0x80ff0001,                                     // mov r1.z, r1.w
        0x03000002, 0x80070000, 0x80e40001, 0xa0e40002,                         // add r0.xyz, r1, c2
        0x03000008, 0x80110001, 0x80e40000, 0xa0e40003,                         // dp3_sat r1.x, r0, c3
        0x03000008, 0x80120001, 0x80e40000, 0xa0e40004,                         // dp3_sat r1.y, r0, c4
        0x0400005a, 0x80140001, 0x80e40000, 0xa0e40005, 0xa0aa0005,             // dp2add_sat r1.z, r0, c5, c5.z
        0x02000001, 0x80080001, 0xa0aa0001,                                     // mov r1.w, c1.z
        0x02000001, 0x800f0800, 0x80e40001,                                     // mov oC0, r1
        0x0000ffff
    };

    /* UYVY to RGB is same as YUY2 above except for the order of yuv components:

        // YUY2 if not defined
        #define UYVY
        ...
     */
    static DWORD const aPSCodeUYVYtoRGB[] =
    {
        0xffff0200,                                                             // ps_2_0
        0x05000051, 0xa00f0001, 0x3f000000, 0x00000000, 0x3f800000, 0xbf000000, // def c1, 0.5, 0, 1, -0.5
        0x05000051, 0xa00f0002, 0xbd8068dc, 0xbf008312, 0xbf008312, 0x00000000, // def c2, -0.0627000034, -0.501999974, -0.501999974, 0
        0x05000051, 0xa00f0003, 0x3f950a81, 0x00000000, 0x3fcc4a9d, 0x00000000, // def c3, 1.16438305, 0, 1.59602702, 0
        0x05000051, 0xa00f0004, 0x3f950a81, 0xbec89507, 0xbf501eac, 0x00000000, // def c4, 1.16438305, -0.391761988, -0.812968016, 0
        0x05000051, 0xa00f0005, 0x3f950a81, 0x40011a54, 0x00000000, 0x00000000, // def c5, 1.16438305, 2.01723194, 0, 0
        0x0200001f, 0x80000000, 0xb0030000,                                     // dcl t0.xy
        0x0200001f, 0x90000000, 0xa00f0800,                                     // dcl_2d s0
        0x03000005, 0x80080000, 0xb0000000, 0xa0000000,                         // mul r0.w, t0.x, c0.x
        0x03000005, 0x80010000, 0x80ff0000, 0xa0000001,                         // mul r0.x, r0.w, c1.x
        0x02000013, 0x80020000, 0x80000000,                                     // frc r0.y, r0.x
        0x04000058, 0x80040000, 0x81550000, 0xa0550001, 0xa0aa0001,             // cmp r0.z, -r0.y, c1.y, c1.z
        0x03000002, 0x80020000, 0x80000000, 0x81550000,                         // add r0.y, r0.x, -r0.y
        0x03000005, 0x80010001, 0x80000000, 0xa0aa0000,                         // mul r1.x, r0.x, c0.z
        0x04000058, 0x80010000, 0x80ff0000, 0xa0550001, 0x80aa0000,             // cmp r0.x, r0.w, c1.y, r0.z
        0x03000002, 0x80010000, 0x80000000, 0x80550000,                         // add r0.x, r0.x, r0.y
        0x04000004, 0x80010000, 0x80ff0000, 0xa0000001, 0x81000000,             // mad r0.x, r0.w, c1.x, -r0.x
        0x03000002, 0x80010000, 0x80000000, 0xa0ff0001,                         // add r0.x, r0.x, c1.w
        0x02000001, 0x80020001, 0xb0550000,                                     // mov r1.y, t0.y
        0x03000042, 0x800f0001, 0x80e40001, 0xa0e40800,                         // texld r1, r1, s0
        0x04000058, 0x80010000, 0x80000000, 0x80ff0001, 0x80550001,             // cmp r0.x, r0.x, r1.w, r1.y
        0x02000001, 0x80060000, 0x80c90001,                                     // mov r0.yz, r1.yzxw
        0x03000002, 0x80070000, 0x80e40000, 0xa0e40002,                         // add r0.xyz, r0, c2
        0x03000008, 0x80110001, 0x80e40000, 0xa0e40003,                         // dp3_sat r1.x, r0, c3
        0x03000008, 0x80120001, 0x80e40000, 0xa0e40004,                         // dp3_sat r1.y, r0, c4
        0x0400005a, 0x80140001, 0x80e40000, 0xa0e40005, 0xa0aa0005,             // dp2add_sat r1.z, r0, c5, c5.z
        0x02000001, 0x80080001, 0xa0aa0001,                                     // mov r1.w, c1.z
        0x02000001, 0x800f0800, 0x80e40001,                                     // mov oC0, r1
        0x0000ffff
    };

    /* RGB to YUY2.
     * UYVY is not defined.

        static const float3x3 bgrCoeffs =
        {
            0.0977f,  0.4375f, -0.0703f,
            0.5039f, -0.2891f, -0.3672f,
            0.2578f, -0.1484f,  0.4375f
        };

        static const float3 yuvShift = { 0.0647f, 0.5039f, 0.5039f };

        PS_OUTPUT PS(VS_OUTPUT In)
        {
            PS_OUTPUT Output;

            // 4 bytes of an YUV macropixel contain 2 source pixels in X.
            // I.e. each YUV texture target pixel is computed from 2 source pixels.
            // The target texture pixels are located in the [0; width / 2 - 1] range.

            // In.TexCoord are in [0;1] range, applicable both to the source and the target textures.
            float2 texCoordDst = In.TexCoord;

            // Convert to the target coords in pixels: xPixel = TexCoord.x * Width.
            float xTargetPixel = texCoordDst.x * gTextureInfo.x;

            float4 bgraOutputPixel;
            if (xTargetPixel < gTextureInfo.x / 2.0f)
            {
                // Target texture is half width, i.e. it contains data in pixels [0; width / 2 - 1].
                // Compute the source texture coords for the pixels which will be used to compute the target pixel.
                float2 texCoordSrc = texCoordDst;
                texCoordSrc.x *= 2.0f;

                // Even pixel. Fetch two BGRA source pixels.
                float4 texColor0 = tex2D(sSource, texCoordSrc);

                // Advance one pixel (+ 1/Width)
                texCoordSrc.x += gTextureInfo.z;
                float4 texColor1 = tex2D(sSource, texCoordSrc);

                // Compute y0, u, y1, v components
                // https://docs.microsoft.com/en-us/windows/win32/medfound/recommended-8-bit-yuv-formats-for-video-rendering#converting-rgb888-to-yuv-444
                //
                // For R,G,B and Y,U,V in [0;255]
                // Y = ( (  66 * R + 129 * G +  25 * B + 128) >> 8) +  16
                // U = ( ( -38 * R -  74 * G + 112 * B + 128) >> 8) + 128
                // V = ( ( 112 * R -  94 * G -  18 * B + 128) >> 8) + 128
                //
                // For r,g,b and y,u,v in [0;1.0]
                // y =  0.2578 * r + 0.5039 * g + 0.0977 * b + 0.0647
                // u = -0.1484 * r - 0.2891 * g + 0.4375 * b + 0.5039
                // v =  0.4375 * r - 0.3672 * g - 0.0703 * b + 0.5039

                float3 yuv0 = mul(texColor0.bgr, bgrCoeffs);
                yuv0 -= yuvShift;

                float3 yuv1 = mul(texColor1.bgr, bgrCoeffs);
                yuv1 -= yuvShift;

                float y0 = yuv0.b;
                float  u = (yuv0.g + yuv1.g) / 2.0f;
                float y1 = yuv1.b;
                float  v = (yuv0.r + yuv1.r) / 2.0f;

        #ifdef UYVY
                bgraOutputPixel = float4(u, y0, v, y1);
        #else // YUY2
                bgraOutputPixel = float4(y0, u, y1, v);
        #endif
            }
            else
            {
                // [width / 2; width - 1] pixels are not used. Set to something.
                bgraOutputPixel = float4(0.0f, 0.0f, 0.0f, 0.0f);
            }

            // Clamp to [0;1]
            bgraOutputPixel = saturate(bgraOutputPixel);

            // Return RGBA
            Output.Color = bgraOutputPixel;

            return Output;
        }
     */
    static DWORD const aPSCodeRGBtoYUY2[] =
    {
        0xffff0200,                                                             // ps_2_0
        0x05000051, 0xa00f0001, 0xbd84816f, 0xbf00ff97, 0xbf00ff97, 0x00000000, // def c1, -0.0647, -0.503899992, -0.503899992, 0
        0x05000051, 0xa00f0002, 0xbe80ff97, 0x00000000, 0xbd04816f, 0x00000000, // def c2, -0.251949996, 0, -0.03235, 0
        0x05000051, 0xa00f0003, 0x3dc816f0, 0x3f00ff97, 0x3e83fe5d, 0x00000000, // def c3, 0.0976999998, 0.503899992, 0.257800013, 0
        0x05000051, 0xa00f0004, 0x3ee00000, 0xbe9404ea, 0xbe17f62b, 0x00000000, // def c4, 0.4375, -0.289099991, -0.148399994, 0
        0x05000051, 0xa00f0005, 0xbd8ff972, 0xbebc01a3, 0x3ee00000, 0x00000000, // def c5, -0.0702999979, -0.367199987, 0.4375, 0
        0x05000051, 0xa00f0006, 0x3f000000, 0x40000000, 0x3f800000, 0xbf00ff97, // def c6, 0.5, 2, 1, -0.503899992
        0x05000051, 0xa00f0007, 0x3f000000, 0x3f800000, 0x3f000000, 0x00000000, // def c7, 0.5, 1, 0.5, 0
        0x0200001f, 0x80000000, 0xb0030000,                                     // dcl t0.xy
        0x0200001f, 0x90000000, 0xa00f0800,                                     // dcl_2d s0
        0x03000005, 0x80030000, 0xb0e40000, 0xa0c90006,                         // mul r0.xy, t0, c6.yzxw
        0x02000001, 0x80030001, 0xa0e40006,                                     // mov r1.xy, c6
        0x04000004, 0x80010002, 0xb0000000, 0x80550001, 0xa0aa0000,             // mad r2.x, t0.x, r1.y, c0.z
        0x02000001, 0x80020002, 0xb0550000,                                     // mov r2.y, t0.y
        0x03000042, 0x800f0000, 0x80e40000, 0xa0e40800,                         // texld r0, r0, s0
        0x03000042, 0x800f0002, 0x80e40002, 0xa0e40800,                         // texld r2, r2, s0
        0x03000005, 0x80080000, 0x80aa0000, 0xa0000003,                         // mul r0.w, r0.z, c3.x
        0x04000004, 0x80080000, 0x80550000, 0xa0550003, 0x80ff0000,             // mad r0.w, r0.y, c3.y, r0.w
        0x04000004, 0x80010003, 0x80000000, 0xa0aa0003, 0x80ff0000,             // mad r3.x, r0.x, c3.z, r0.w
        0x03000005, 0x80080000, 0x80aa0000, 0xa0000004,                         // mul r0.w, r0.z, c4.x
        0x04000004, 0x80080000, 0x80550000, 0xa0550004, 0x80ff0000,             // mad r0.w, r0.y, c4.y, r0.w
        0x04000004, 0x80020003, 0x80000000, 0xa0aa0004, 0x80ff0000,             // mad r3.y, r0.x, c4.z, r0.w
        0x03000005, 0x80080002, 0x80aa0000, 0xa0000005,                         // mul r2.w, r0.z, c5.x
        0x04000004, 0x80080002, 0x80550000, 0xa0550005, 0x80ff0002,             // mad r2.w, r0.y, c5.y, r2.w
        0x04000004, 0x80040003, 0x80000000, 0xa0aa0005, 0x80ff0002,             // mad r3.z, r0.x, c5.z, r2.w
        0x03000002, 0x80070000, 0x80e40003, 0xa0e40001,                         // add r0.xyz, r3, c1
        0x02000001, 0x80080000, 0xa0ff0006,                                     // mov r0.w, c6.w
        0x03000005, 0x80080002, 0x80aa0002, 0xa0000003,                         // mul r2.w, r2.z, c3.x
        0x04000004, 0x80080002, 0x80550002, 0xa0550003, 0x80ff0002,             // mad r2.w, r2.y, c3.y, r2.w
        0x04000004, 0x80040003, 0x80000002, 0xa0aa0003, 0x80ff0002,             // mad r3.z, r2.x, c3.z, r2.w
        0x03000005, 0x80080002, 0x80aa0002, 0xa0000004,                         // mul r2.w, r2.z, c4.x
        0x04000004, 0x80080002, 0x80550002, 0xa0550004, 0x80ff0002,             // mad r2.w, r2.y, c4.y, r2.w
        0x04000004, 0x80010003, 0x80000002, 0xa0aa0004, 0x80ff0002,             // mad r3.x, r2.x, c4.z, r2.w
        0x03000005, 0x80080003, 0x80aa0002, 0xa0000005,                         // mul r3.w, r2.z, c5.x
        0x04000004, 0x80080003, 0x80550002, 0xa0550005, 0x80ff0003,             // mad r3.w, r2.y, c5.y, r3.w
        0x04000004, 0x80020003, 0x80000002, 0xa0aa0005, 0x80ff0003,             // mad r3.y, r2.x, c5.z, r3.w
        0x03000002, 0x80050002, 0x80c90000, 0x80e40003,                         // add r2.xz, r0.yzxw, r3
        0x03000002, 0x80020002, 0x80ff0000, 0x80550003,                         // add r2.y, r0.w, r3.y
        0x02000001, 0x80110000, 0x80aa0000,                                     // mov_sat r0.x, r0.z
        0x02000001, 0x80070003, 0xa0e40007,                                     // mov r3.xyz, c7
        0x04000004, 0x80160000, 0x80d20002, 0x80d20003, 0xa0d20002,             // mad_sat r0.yz, r2.zxyw, r3.zxyw, c2.zxyw
        0x04000004, 0x80180000, 0x80aa0002, 0x80aa0003, 0xa0aa0002,             // mad_sat r0.w, r2.z, r3.z, c2.z
        0x03000005, 0x80010001, 0x80000001, 0xa0000000,                         // mul r1.x, r1.x, c0.x
        0x04000004, 0x80010001, 0xb0000000, 0xa0000000, 0x81000001,             // mad r1.x, t0.x, c0.x, -r1.x
        0x04000058, 0x800f0000, 0x80000001, 0xa0ff0003, 0x80e40000,             // cmp r0, r1.x, c3.w, r0
        0x02000001, 0x800f0800, 0x80e40000,                                     // mov oC0, r0
        0x0000ffff
    };

    /* RGB to UYVY is same as YUY2 above except for the order of yuv components.
     * UYVY is defined.
     */
    static DWORD const aPSCodeRGBtoUYVY[] =
    {
        0xffff0200,                                                             // ps_2_0
        0x05000051, 0xa00f0001, 0xbd84816f, 0xbf00ff97, 0xbf00ff97, 0x00000000, // def c1, -0.0647, -0.503899992, -0.503899992, 0
        0x05000051, 0xa00f0002, 0xbe80ff97, 0xbd04816f, 0x00000000, 0x00000000, // def c2, -0.251949996, -0.03235, 0, 0
        0x05000051, 0xa00f0003, 0x3dc816f0, 0x3f00ff97, 0x3e83fe5d, 0x00000000, // def c3, 0.0976999998, 0.503899992, 0.257800013, 0
        0x05000051, 0xa00f0004, 0x3ee00000, 0xbe9404ea, 0xbe17f62b, 0x00000000, // def c4, 0.4375, -0.289099991, -0.148399994, 0
        0x05000051, 0xa00f0005, 0xbd8ff972, 0xbebc01a3, 0x3ee00000, 0x00000000, // def c5, -0.0702999979, -0.367199987, 0.4375, 0
        0x05000051, 0xa00f0006, 0x3f000000, 0x40000000, 0x3f800000, 0xbf00ff97, // def c6, 0.5, 2, 1, -0.503899992
        0x05000051, 0xa00f0007, 0x3f000000, 0x3f000000, 0x3f800000, 0x00000000, // def c7, 0.5, 0.5, 1, 0
        0x0200001f, 0x80000000, 0xb0030000,                                     // dcl t0.xy
        0x0200001f, 0x90000000, 0xa00f0800,                                     // dcl_2d s0
        0x03000005, 0x80030000, 0xb0e40000, 0xa0c90006,                         // mul r0.xy, t0, c6.yzxw
        0x02000001, 0x80030001, 0xa0e40006,                                     // mov r1.xy, c6
        0x04000004, 0x80010002, 0xb0000000, 0x80550001, 0xa0aa0000,             // mad r2.x, t0.x, r1.y, c0.z
        0x02000001, 0x80020002, 0xb0550000,                                     // mov r2.y, t0.y
        0x03000042, 0x800f0000, 0x80e40000, 0xa0e40800,                         // texld r0, r0, s0
        0x03000042, 0x800f0002, 0x80e40002, 0xa0e40800,                         // texld r2, r2, s0
        0x03000005, 0x80080000, 0x80aa0000, 0xa0000003,                         // mul r0.w, r0.z, c3.x
        0x04000004, 0x80080000, 0x80550000, 0xa0550003, 0x80ff0000,             // mad r0.w, r0.y, c3.y, r0.w
        0x04000004, 0x80010003, 0x80000000, 0xa0aa0003, 0x80ff0000,             // mad r3.x, r0.x, c3.z, r0.w
        0x03000005, 0x80080000, 0x80aa0000, 0xa0000004,                         // mul r0.w, r0.z, c4.x
        0x04000004, 0x80080000, 0x80550000, 0xa0550004, 0x80ff0000,             // mad r0.w, r0.y, c4.y, r0.w
        0x04000004, 0x80020003, 0x80000000, 0xa0aa0004, 0x80ff0000,             // mad r3.y, r0.x, c4.z, r0.w
        0x03000005, 0x80080002, 0x80aa0000, 0xa0000005,                         // mul r2.w, r0.z, c5.x
        0x04000004, 0x80080002, 0x80550000, 0xa0550005, 0x80ff0002,             // mad r2.w, r0.y, c5.y, r2.w
        0x04000004, 0x80040003, 0x80000000, 0xa0aa0005, 0x80ff0002,             // mad r3.z, r0.x, c5.z, r2.w
        0x03000002, 0x80070000, 0x80e40003, 0xa0e40001,                         // add r0.xyz, r3, c1
        0x02000001, 0x80080000, 0xa0ff0006,                                     // mov r0.w, c6.w
        0x03000005, 0x80080002, 0x80aa0002, 0xa0000003,                         // mul r2.w, r2.z, c3.x
        0x04000004, 0x80080002, 0x80550002, 0xa0550003, 0x80ff0002,             // mad r2.w, r2.y, c3.y, r2.w
        0x04000004, 0x80020003, 0x80000002, 0xa0aa0003, 0x80ff0002,             // mad r3.y, r2.x, c3.z, r2.w
        0x03000005, 0x80080002, 0x80aa0002, 0xa0000004,                         // mul r2.w, r2.z, c4.x
        0x04000004, 0x80080002, 0x80550002, 0xa0550004, 0x80ff0002,             // mad r2.w, r2.y, c4.y, r2.w
        0x04000004, 0x80010003, 0x80000002, 0xa0aa0004, 0x80ff0002,             // mad r3.x, r2.x, c4.z, r2.w
        0x03000005, 0x80080003, 0x80aa0002, 0xa0000005,                         // mul r3.w, r2.z, c5.x
        0x04000004, 0x80080003, 0x80550002, 0xa0550005, 0x80ff0003,             // mad r3.w, r2.y, c5.y, r3.w
        0x04000004, 0x80040003, 0x80000002, 0xa0aa0005, 0x80ff0003,             // mad r3.z, r2.x, c5.z, r3.w
        0x03000002, 0x80010002, 0x80550000, 0x80000003,                         // add r2.x, r0.y, r3.x
        0x03000002, 0x80020002, 0x80000000, 0x80550003,                         // add r2.y, r0.x, r3.y
        0x03000002, 0x80040002, 0x80ff0000, 0x80aa0003,                         // add r2.z, r0.w, r3.z
        0x02000001, 0x80120000, 0x80aa0000,                                     // mov_sat r0.y, r0.z
        0x02000001, 0x80070003, 0xa0e40007,                                     // mov r3.xyz, c7
        0x04000004, 0x80110000, 0x80000002, 0x80000003, 0xa0000002,             // mad_sat r0.x, r2.x, r3.x, c2.x
        0x04000004, 0x80140000, 0x80550002, 0x80550003, 0xa0550002,             // mad_sat r0.z, r2.y, r3.y, c2.y
        0x04000004, 0x80180000, 0x80aa0002, 0x80aa0003, 0xa0aa0002,             // mad_sat r0.w, r2.z, r3.z, c2.z
        0x03000005, 0x80010001, 0x80000001, 0xa0000000,                         // mul r1.x, r1.x, c0.x
        0x04000004, 0x80010001, 0xb0000000, 0xa0000000, 0x81000001,             // mad r1.x, t0.x, c0.x, -r1.x
        0x04000058, 0x800f0000, 0x80000001, 0xa0ff0003, 0x80e40000,             // cmp r0, r1.x, c3.w, r0
        0x02000001, 0x800f0800, 0x80e40000,                                     // mov oC0, r0
        0x0000ffff
    };

    switch (d3dfmtFrom)
    {
        /*
         * Emulated to ARGB
         */
        case D3DFMT_A8B8G8R8:
        {
            if (d3dfmtTo == D3DFMT_A8R8G8B8)
            {
                pResult->paVS             = aVSCode;
                pResult->paPS             = aPSCodeSwapRB;

                return D3D_OK;
            }
        } break;
        case D3DFMT_UYVY:
        {
            if (d3dfmtTo == D3DFMT_A8R8G8B8)
            {
                pResult->paVS             = aVSCode;
                pResult->paPS             = aPSCodeUYVYtoRGB;

                return D3D_OK;
            }
        } break;
        case D3DFMT_YUY2:
        {
            if (d3dfmtTo == D3DFMT_A8R8G8B8)
            {
                pResult->paVS             = aVSCode;
                pResult->paPS             = aPSCodeYUY2toRGB;

                return D3D_OK;
            }
        } break;

        /*
         * ARGB to emulated.
         */
        case D3DFMT_A8R8G8B8:
        {
            if (d3dfmtTo == D3DFMT_A8B8G8R8)
            {
                pResult->paVS             = aVSCode;
                pResult->paPS             = aPSCodeSwapRB;

                return D3D_OK;
            }
            else if (d3dfmtTo == D3DFMT_UYVY)
            {
                pResult->paVS             = aVSCode;
                pResult->paPS             = aPSCodeRGBtoUYVY;

                return D3D_OK;
            }
            else if (d3dfmtTo == D3DFMT_YUY2)
            {
                pResult->paVS             = aVSCode;
                pResult->paPS             = aPSCodeRGBtoYUY2;

                return D3D_OK;
            }
        } break;

        default:
            break;
    }

    return E_NOTIMPL;
}

class D3D9Conversion
{
    private:
        IDirect3DDevice9Ex *mpDevice;

        /* State objects. */
        IDirect3DVertexBuffer9      *mpVB;
        IDirect3DVertexDeclaration9 *mpVertexDecl;
        IDirect3DVertexShader9      *mpVS;
        IDirect3DPixelShader9       *mpPS;

        D3D9ConversionParameters mParameters;

        typedef struct D3DSamplerState
        {
            D3DSAMPLERSTATETYPE Type;
            DWORD               Value;
        } D3DSamplerState;

        /* Saved context. */
        struct
        {
            DWORD                   dwCull;
            DWORD                   dwZEnable;
            IDirect3DSurface9      *pRT;
            IDirect3DVertexShader9 *pVS;
            IDirect3DPixelShader9  *pPS;
            IDirect3DBaseTexture9  *pTexture;
            float                  aVSConstantData[4];
            float                  aPSConstantData[4];
            D3DSamplerState        aSamplerState[3];
        } mSaved;

        void destroyConversion();
        HRESULT saveContextState();
        HRESULT restoreContextState(PVMSVGA3DCONTEXT pContext);
        HRESULT initConversion();
        HRESULT setConversionState(IDirect3DTexture9 *pSourceTexture,
                                   uint32_t cWidth,
                                   uint32_t cHeight);

    public:
        enum Direction
        {
            FromEmulated,
            ToEmulated,
        };

        D3D9Conversion(IDirect3DDevice9Ex *pDevice);
        ~D3D9Conversion();

        HRESULT SelectConversion(D3DFORMAT d3dfmtFrom, D3DFORMAT d3dfmtTo);
        HRESULT ConvertTexture(PVMSVGA3DCONTEXT pContext,
                               PVMSVGA3DSURFACE pSurface,
                               Direction enmDirection);
};

D3D9Conversion::D3D9Conversion(IDirect3DDevice9Ex *pDevice)
    :
    mpDevice(pDevice),
    mpVB(0),
    mpVertexDecl(0),
    mpVS(0),
    mpPS(0)
{
    mParameters.paVS = 0;
    mParameters.paPS = 0;
    mSaved.dwCull = D3DCULL_NONE;
    mSaved.dwZEnable = D3DZB_FALSE;
    mSaved.pRT = 0;
    mSaved.pVS = 0;
    mSaved.pPS = 0;
    mSaved.pTexture = 0;
    RT_ZERO(mSaved.aVSConstantData);
    RT_ZERO(mSaved.aPSConstantData);
    mSaved.aSamplerState[0].Type = D3DSAMP_MAGFILTER;
    mSaved.aSamplerState[0].Value = D3DTEXF_POINT;
    mSaved.aSamplerState[1].Type = D3DSAMP_MINFILTER;
    mSaved.aSamplerState[1].Value = D3DTEXF_POINT;
    mSaved.aSamplerState[2].Type = D3DSAMP_MIPFILTER;
    mSaved.aSamplerState[2].Value = D3DTEXF_NONE;
}

D3D9Conversion::~D3D9Conversion()
{
    destroyConversion();
}

void D3D9Conversion::destroyConversion()
{
    D3D_RELEASE(mpVertexDecl);
    D3D_RELEASE(mpVB);
    D3D_RELEASE(mpVS);
    D3D_RELEASE(mpPS);
}

HRESULT D3D9Conversion::saveContextState()
{
    /*
     * This is probably faster than
     * IDirect3DStateBlock9 *mpStateBlock;
     * mpDevice->CreateStateBlock(D3DSBT_ALL, &mpStateBlock);
     */
    HRESULT hr = mpDevice->GetRenderState(D3DRS_CULLMODE, &mSaved.dwCull);
    AssertReturn(hr == D3D_OK, hr);

    hr = mpDevice->GetRenderState(D3DRS_ZENABLE, &mSaved.dwZEnable);
    AssertReturn(hr == D3D_OK, hr);

    hr = mpDevice->GetRenderTarget(0, &mSaved.pRT);
    AssertReturn(hr == D3D_OK, hr);

    hr = mpDevice->GetVertexShader(&mSaved.pVS);
    AssertReturn(hr == D3D_OK, hr);

    hr = mpDevice->GetPixelShader(&mSaved.pPS);
    AssertReturn(hr == D3D_OK, hr);

    hr = mpDevice->GetTexture(0, &mSaved.pTexture);
    AssertReturn(hr == D3D_OK, hr);

    hr = mpDevice->GetVertexShaderConstantF(0, mSaved.aVSConstantData, 1);
    AssertReturn(hr == D3D_OK, hr);

    hr = mpDevice->GetPixelShaderConstantF(0, mSaved.aPSConstantData, 1);
    AssertReturn(hr == D3D_OK, hr);

    for (uint32_t i = 0; i < RT_ELEMENTS(mSaved.aSamplerState); ++i)
    {
        hr = mpDevice->GetSamplerState(0, mSaved.aSamplerState[i].Type, &mSaved.aSamplerState[i].Value);
        AssertReturn(hr == D3D_OK, hr);
    }

    return hr;
}

HRESULT D3D9Conversion::restoreContextState(PVMSVGA3DCONTEXT pContext)
{
    HRESULT hr = mpDevice->SetRenderState(D3DRS_CULLMODE, mSaved.dwCull);
    Assert(hr == D3D_OK);

    hr = mpDevice->SetRenderState(D3DRS_ZENABLE, mSaved.dwZEnable);
    Assert(hr == D3D_OK);

    hr = mpDevice->SetRenderTarget(0, mSaved.pRT);
    D3D_RELEASE(mSaved.pRT); /* GetRenderTarget increases the internal reference count. */
    Assert(hr == D3D_OK);

    hr = mpDevice->SetVertexDeclaration(pContext->d3dState.pVertexDecl);
    Assert(hr == D3D_OK);

    hr = mpDevice->SetVertexShader(mSaved.pVS);
    Assert(hr == D3D_OK);

    hr = mpDevice->SetPixelShader(mSaved.pPS);
    Assert(hr == D3D_OK);

    hr = mpDevice->SetTexture(0, mSaved.pTexture);
    D3D_RELEASE(mSaved.pTexture); /* GetTexture increases the internal reference count. */
    Assert(hr == D3D_OK);

    hr = mpDevice->SetVertexShaderConstantF(0, mSaved.aVSConstantData, 1);
    Assert(hr == D3D_OK);

    hr = mpDevice->SetPixelShaderConstantF(0, mSaved.aPSConstantData, 1);
    Assert(hr == D3D_OK);

    for (uint32_t i = 0; i < RT_ELEMENTS(mSaved.aSamplerState); ++i)
    {
        hr = mpDevice->SetSamplerState(0, mSaved.aSamplerState[i].Type, mSaved.aSamplerState[i].Value);
        Assert(hr == D3D_OK);
    }

    return hr;
}

HRESULT D3D9Conversion::initConversion()
{
    static D3DVERTEXELEMENT9 const aVertexElements[] =
    {
        {0,  0, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0,  8, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        D3DDECL_END()
    };

    HRESULT hr = mpDevice->CreateVertexDeclaration(aVertexElements, &mpVertexDecl);
    AssertReturn(hr == D3D_OK, hr);

    hr = mpDevice->CreateVertexBuffer(6 * sizeof(Vertex),
                                      0, /* D3DUSAGE_* */
                                      0, /* FVF */
                                      D3DPOOL_DEFAULT,
                                      &mpVB,
                                      0);
    AssertReturn(hr == D3D_OK, hr);

    hr = mpDevice->CreateVertexShader(mParameters.paVS, &mpVS);
    AssertReturn(hr == D3D_OK, hr);

    hr = mpDevice->CreatePixelShader(mParameters.paPS, &mpPS);
    AssertReturn(hr == D3D_OK, hr);

    return hr;
}

HRESULT D3D9Conversion::setConversionState(IDirect3DTexture9 *pSourceTexture,
                                           uint32_t cWidth,
                                           uint32_t cHeight)
{
    /* Subtract 0.5 to line up the pixel centers with texels
     * https://docs.microsoft.com/en-us/windows/win32/direct3d9/directly-mapping-texels-to-pixels
     */
    float const xLeft   = -0.5f;
    float const xRight  = (float)(cWidth - 1) - 0.5f;
    float const yTop    = -0.5f;
    float const yBottom = (float)(cHeight - 1) - 0.5f;

    Vertex const aVertices[] =
    {
        { xLeft,  yTop,    0.0f, 0.0f},
        { xRight, yTop,    1.0f, 0.0f},
        { xRight, yBottom, 1.0f, 1.0f},

        { xLeft,  yTop,    0.0f, 0.0f},
        { xRight, yBottom, 1.0f, 1.0f},
        { xLeft,  yBottom, 0.0f, 1.0f},
    };

    HRESULT hr = d3dCopyToVertexBuffer(mpVB, aVertices, sizeof(aVertices));
    AssertReturn(hr == D3D_OK, hr);

    /* No need to save the stream source, because vmsvga3dDrawPrimitives always sets it. */
    hr = mpDevice->SetStreamSource(0, mpVB, 0, sizeof(Vertex));
    AssertReturn(hr == D3D_OK, hr);

    /* Stored in pContext->d3dState.pVertexDecl. */
    hr = mpDevice->SetVertexDeclaration(mpVertexDecl);
    AssertReturn(hr == D3D_OK, hr);

    /* Saved by saveContextState. */
    hr = mpDevice->SetVertexShader(mpVS);
    AssertReturn(hr == D3D_OK, hr);

    /* Saved by saveContextState. */
    hr = mpDevice->SetPixelShader(mpPS);
    AssertReturn(hr == D3D_OK, hr);

    /* Saved by saveContextState. */
    hr = mpDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    AssertReturn(hr == D3D_OK, hr);

    /* Saved by saveContextState. */
    hr = mpDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    AssertReturn(hr == D3D_OK, hr);

    /* Saved by saveContextState. */
    hr = mpDevice->SetTexture(0, pSourceTexture);
    AssertReturn(hr == D3D_OK, hr);

    float aTextureInfo[4];
    aTextureInfo[0] = (float)cWidth;
    aTextureInfo[1] = (float)cHeight;
    aTextureInfo[2] = 1.0f / (float)cWidth;  /* Pixel width in texture coords. */
    aTextureInfo[3] = 1.0f / (float)cHeight; /* Pixel height in texture coords. */

    /* Saved by saveContextState. */
    hr = mpDevice->SetVertexShaderConstantF(0, aTextureInfo, 1);
    AssertReturn(hr == D3D_OK, hr);

    /* Saved by saveContextState. */
    hr = mpDevice->SetPixelShaderConstantF(0, aTextureInfo, 1);
    AssertReturn(hr == D3D_OK, hr);

    /* Saved by saveContextState. */
    hr = mpDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    AssertReturn(hr == D3D_OK, hr);
    hr = mpDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    AssertReturn(hr == D3D_OK, hr);
    hr = mpDevice->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    AssertReturn(hr == D3D_OK, hr);

    return hr;
}


HRESULT D3D9Conversion::SelectConversion(D3DFORMAT d3dfmtFrom, D3DFORMAT d3dfmtTo)
{
    /** @todo d3d9SelectConversion should be a member. Move the code here? */
    HRESULT hr = d3d9SelectConversion(d3dfmtFrom, d3dfmtTo, &mParameters);
    return hr;
}

HRESULT D3D9Conversion::ConvertTexture(PVMSVGA3DCONTEXT pContext,
                                       PVMSVGA3DSURFACE pSurface,
                                       Direction enmDirection)
{
    IDirect3DTexture9 *pSourceTexture;
    IDirect3DTexture9 *pTargetTexture;
    D3D9TextureType enmTargetType;

    if (enmDirection == FromEmulated)
    {
        pSourceTexture = pSurface->emulated.pTexture;
        pTargetTexture = pSurface->u.pTexture;
        enmTargetType = D3D9TextureType_Texture;
    }
    else if (enmDirection == ToEmulated)
    {
        pSourceTexture = pSurface->u.pTexture;
        pTargetTexture = pSurface->emulated.pTexture;
        enmTargetType = D3D9TextureType_Emulated;
    }
    else
        AssertFailedReturn(E_INVALIDARG);

    AssertPtrReturn(pSourceTexture, E_INVALIDARG);
    AssertPtrReturn(pTargetTexture, E_INVALIDARG);

    HRESULT hr = saveContextState();
    if (hr == D3D_OK)
    {
        hr = initConversion();
        if (hr == D3D_OK)
        {
            uint32_t const cWidth = pSurface->paMipmapLevels[0].mipmapSize.width;
            uint32_t const cHeight = pSurface->paMipmapLevels[0].mipmapSize.height;

            hr = setConversionState(pSourceTexture, cWidth, cHeight);
            if (hr == D3D_OK)
            {
                hr = mpDevice->BeginScene();
                Assert(hr == D3D_OK);
                if (hr == D3D_OK)
                {
                    for (DWORD iFace = 0; iFace < pSurface->cFaces; ++iFace)
                    {
                        DWORD const cMipLevels = pTargetTexture->GetLevelCount();
                        for (DWORD iMipmap = 0; iMipmap < cMipLevels && hr == D3D_OK; ++iMipmap)
                        {
                            IDirect3DSurface9 *pRT = NULL;
                            hr = D3D9GetTextureLevel(pSurface, enmTargetType, iFace, iMipmap, &pRT);
                            Assert(hr == D3D_OK);
                            if (hr == D3D_OK)
                            {
                                hr = mpDevice->SetRenderTarget(0, pRT);
                                Assert(hr == D3D_OK);
                                if (hr == D3D_OK)
                                {
                                    hr = mpDevice->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2);
                                    Assert(hr == D3D_OK);
                                }

                                D3D_RELEASE(pRT);
                            }
                        }
                    }

                    hr = mpDevice->EndScene();
                    Assert(hr == D3D_OK);
                }
            }
        }

        hr = restoreContextState(pContext);
    }

    destroyConversion();

    return hr;
}


HRESULT D3D9UpdateTexture(PVMSVGA3DCONTEXT pContext,
                          PVMSVGA3DSURFACE pSurface)
{
    HRESULT hr = S_OK;

    if (pSurface->formatD3D == pSurface->d3dfmtRequested)
    {
        hr = pContext->pDevice->UpdateTexture(pSurface->bounce.pTexture, pSurface->u.pTexture);
    }
    else
    {
        if (pSurface->enmD3DResType == VMSVGA3D_D3DRESTYPE_TEXTURE)
        {
            hr = pContext->pDevice->UpdateTexture(pSurface->bounce.pTexture, pSurface->emulated.pTexture);
            if (hr == D3D_OK)
            {
                D3D9Conversion conv(pContext->pDevice);
                hr = conv.SelectConversion(pSurface->d3dfmtRequested, pSurface->formatD3D);
                if (hr == D3D_OK)
                {
                    hr = conv.ConvertTexture(pContext, pSurface, D3D9Conversion::FromEmulated);
                }
            }
        }
        else
        {
            /** @todo Cubemaps and maybe volume textures. */
            hr = pContext->pDevice->UpdateTexture(pSurface->bounce.pTexture, pSurface->u.pTexture);
        }
    }

    return hr;
}

HRESULT D3D9GetSurfaceLevel(PVMSVGA3DSURFACE pSurface,
                            uint32_t uFace,
                            uint32_t uMipmap,
                            bool fBounce,
                            IDirect3DSurface9 **ppD3DSurface)
{
    HRESULT hr;

    if (   pSurface->enmD3DResType == VMSVGA3D_D3DRESTYPE_CUBE_TEXTURE
        || pSurface->enmD3DResType == VMSVGA3D_D3DRESTYPE_TEXTURE)
    {
        D3D9TextureType enmType;
        if (fBounce)
            enmType = D3D9TextureType_Bounce;
        else if (pSurface->formatD3D != pSurface->d3dfmtRequested)
            enmType = D3D9TextureType_Emulated;
        else
            enmType = D3D9TextureType_Texture;

        hr = D3D9GetTextureLevel(pSurface, enmType, uFace, uMipmap, ppD3DSurface);
    }
    else if (pSurface->enmD3DResType == VMSVGA3D_D3DRESTYPE_SURFACE)
    {
        pSurface->u.pSurface->AddRef();
        *ppD3DSurface = pSurface->u.pSurface;
        hr = S_OK;
    }
    else
    {
        AssertMsgFailed(("No surface for type %d\n", pSurface->enmD3DResType));
        hr = E_INVALIDARG;
    }

    return hr;
}

/** Copy the texture content to the bounce texture.
 */
HRESULT D3D9GetRenderTargetData(PVMSVGA3DCONTEXT pContext,
                                PVMSVGA3DSURFACE pSurface,
                                uint32_t uFace,
                                uint32_t uMipmap)
{
    HRESULT hr;

    /* Get the corresponding bounce texture surface. */
    IDirect3DSurface9 *pDst = NULL;
    hr = D3D9GetSurfaceLevel(pSurface, uFace, uMipmap, true, &pDst);
    AssertReturn(hr == D3D_OK, hr);

    /* Get the actual texture surface, emulated or actual. */
    IDirect3DSurface9 *pSrc = NULL;
    hr = D3D9GetSurfaceLevel(pSurface, uFace, uMipmap, false, &pSrc);
    AssertReturnStmt(hr == D3D_OK,
                     D3D_RELEASE(pDst), hr);

    Assert(pDst != pSrc);

    if (pSurface->formatD3D == pSurface->d3dfmtRequested)
    {
        hr = pContext->pDevice->GetRenderTargetData(pSrc, pDst);
        AssertMsg(hr == D3D_OK, ("GetRenderTargetData failed with %x\n", hr));
    }
    else
    {
        D3D9Conversion conv(pContext->pDevice);
        hr = conv.SelectConversion(pSurface->formatD3D, pSurface->d3dfmtRequested);
        if (hr == D3D_OK)
        {
            hr = conv.ConvertTexture(pContext, pSurface, D3D9Conversion::ToEmulated);
        }

        if (hr == D3D_OK)
        {
            hr = pContext->pDevice->GetRenderTargetData(pSrc, pDst);
            AssertMsg(hr == D3D_OK, ("GetRenderTargetData failed with %x\n", hr));
        }
    }

    D3D_RELEASE(pSrc);
    D3D_RELEASE(pDst);
    return hr;
}

D3DFORMAT D3D9GetActualFormat(PVMSVGA3DSTATE pState, D3DFORMAT d3dfmtRequested)
{
    RT_NOREF(pState);

    switch (d3dfmtRequested)
    {
        case D3DFMT_UYVY:
            if (!pState->fSupportedFormatUYVY) return D3DFMT_A8R8G8B8;
            break;
        case D3DFMT_YUY2:
            if (!pState->fSupportedFormatYUY2) return D3DFMT_A8R8G8B8;
            break;
        case D3DFMT_A8B8G8R8:
            if (!pState->fSupportedFormatA8B8G8R8) return D3DFMT_A8R8G8B8;
            break;
        default:
            break;
    }

    /* Use the requested format. No emulation required. */
    return d3dfmtRequested;
}

bool D3D9CheckDeviceFormat(IDirect3D9 *pD3D9, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat)
{
    HRESULT hr = pD3D9->CheckDeviceFormat(D3DADAPTER_DEFAULT,
                                          D3DDEVTYPE_HAL,
                                          D3DFMT_X8R8G8B8,    /* assume standard 32-bit display mode */
                                          Usage,
                                          RType,
                                          CheckFormat);
    return (hr == D3D_OK);
}
