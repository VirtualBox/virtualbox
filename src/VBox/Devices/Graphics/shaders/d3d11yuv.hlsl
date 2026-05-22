/* $Id: d3d11yuv.hlsl 114182 2026-05-22 16:32:53Z vitali.pelenjow@oracle.com $ */
/*
 * I420 conversion from BGRA8 input with scaling to dstW/dstH.
 * Output planes are R8_UNORM (or R16_UNORM) unordered access views.
 *
 * fxc /nologo /Fhd3d11yuv.hlsl.cs_y.h /Ecs_y /Tcs_5_0 d3d11yuv.hlsl
 * fxc /nologo /Fhd3d11yuv.hlsl.cs_uv.h /Ecs_uv /Tcs_5_0 d3d11yuv.hlsl
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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

cbuffer CSParameters : register(b0)
{
    /* Destination Y plane dimensions. */
    uint dstW;
    uint dstH;
    uint pad0;
    uint pad1;

    /* Offset of the scaled input image in the Y plane in output pixels */
    float dstOffX;
    float dstOffY;
    /* 1/scaledW, 1/scaledH where scaledW and scaledH are dimensions of input image after scaling. */
    float invScaledW;
    float invScaledH;
};

/* Input RGB texture at arbitrary resolution and the corresponding sampler. */
Texture2D<float4> gTexIn : register(t0);
SamplerState gSampLinear : register(s0);

/* Output texture for Y */
RWTexture2D<unorm float> gTexOutY : register(u0);

/* Output texture for U */
RWTexture2D<unorm float> gTexOutU : register(u0);
/* Output texture for V */
RWTexture2D<unorm float> gTexOutV : register(u1);

/* Precomputed BT.601 limited-range constants (normalized to [0..1]) for:
 *
 *   y = 16/255  + dot(rgb, [0.299, 0.587, 0.114]) * (219/255)
 *   u = 128/255 + dot(rgb, [-0.168736, -0.331264, 0.5]) * (224/255)
 *   v = 128/255 + dot(rgb, [0.5, -0.418688, -0.081312]) * (224/255)
 */

static const float kYOff = 16.0f / 255.0f;    // 0.0627450980
static const float kCOff = 128.0f / 255.0f;   // 0.5019607843

static const float3 kY601Lim = float3(
    0.299f * (219.0f/255.0f),   // 0.2567882353
    0.587f * (219.0f/255.0f),   // 0.5041294118
    0.114f * (219.0f/255.0f)    // 0.0979058824
);

static const float3 kU601Lim = float3(
    -0.168736f * (224.0f/255.0f), // -0.1482227451
    -0.331264f * (224.0f/255.0f), // -0.2910235294
     0.5f      * (224.0f/255.0f)  //  0.4392156863
);

static const float3 kV601Lim = float3(
     0.5f      * (224.0f/255.0f),  //  0.4392156863
    -0.418688f * (224.0f/255.0f),  // -0.3677885490
    -0.081312f * (224.0f/255.0f)   // -0.0714271373
);

void RGBToYUV601Limited(float3 rgb, out float y, out float u, out float v)
{
    y = saturate(kYOff + dot(rgb, kY601Lim));
    u = saturate(kCOff + dot(rgb, kU601Lim));
    v = saturate(kCOff + dot(rgb, kV601Lim));
}

/* Sample at output pixel center (x,y) in output space. */
float3 SampleScaledRGB(uint x, uint y)
{
    /* Map output pixel (x, y) to normalized input texture coords using precomputed constants. */
    float nx = (((float)x + 0.5f) - dstOffX) * invScaledW;
    float ny = (((float)y + 0.5f) - dstOffY) * invScaledH;

    bool fInside = (nx >= 0.0 && nx <= 1.0 && ny >= 0.0 && ny <= 1.0);
    if (!fInside)
        return float3(0.0f, 0.0f, 0.0f);

    return gTexIn.SampleLevel(gSampLinear, float2(nx, ny), 0).rgb;
}

[numthreads(16, 16, 1)]
void cs_y(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= dstW || id.y >= dstH)
        return;

    float3 rgb = SampleScaledRGB(id.x, id.y);

    float y, u, v;
    RGBToYUV601Limited(rgb, y, u, v);

    gTexOutY[int2(id.xy)] = y;
}

[numthreads(16, 16, 1)]
void cs_uv(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= dstW / 2 || id.y >= dstH / 2)
        return;

    /* Sample 2x2 block of RGB pixels in output luma (y) coordinates */
    uint x0 = id.x * 2;
    uint y0 = id.y * 2;

    float3 p00 = SampleScaledRGB(x0 + 0, y0 + 0);
    float3 p10 = SampleScaledRGB(x0 + 1, y0 + 0);
    float3 p01 = SampleScaledRGB(x0 + 0, y0 + 1);
    float3 p11 = SampleScaledRGB(x0 + 1, y0 + 1);

    float y, u0, v0, u1, v1, u2, v2, u3, v3;
    RGBToYUV601Limited(p00, y, u0, v0);
    RGBToYUV601Limited(p10, y, u1, v1);
    RGBToYUV601Limited(p01, y, u2, v2);
    RGBToYUV601Limited(p11, y, u3, v3);

    gTexOutU[int2(id.xy)] = saturate(0.25f * (u0 + u1 + u2 + u3));
    gTexOutV[int2(id.xy)] = saturate(0.25f * (v0 + v1 + v2 + v3));
}
