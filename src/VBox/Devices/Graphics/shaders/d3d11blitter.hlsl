/* $Id: d3d11blitter.hlsl 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/*
 * Blitter for dxgiBlt/SVGA_3D_CMD_DX_PRESENTBLT.
 *
 * fxc /nologo /Fhd3d11blitter.hlsl.vs.h /Evs_blitter /Tvs_5_0 d3d11blitter.hlsl
 * fxc /nologo /Fhd3d11blitter.hlsl.ps.h /Eps_blitter /Tps_5_0 d3d11blitter.hlsl
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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

Texture2D t;
sampler s;

cbuffer VSParameters
{
    float scaleX;
    float scaleY;
    float shiftX;
    float shiftY;
};

struct VSInput
{
    uint VertexID   : SV_VertexID;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float2 alpha    : TEXCOORD1;
};

VSOutput vs_blitter(VSInput input)
{
    VSOutput output;

    float x = (input.VertexID & 1) ? 1.0f : -1.0f;
    float y = (input.VertexID & 2) ? -1.0f : 1.0f;
    x = x * scaleX + shiftX;
    y = y * scaleY + shiftY;
    output.position = float4(x, y, 0.0f, 1.0f);

    output.texcoord.x = (input.VertexID & 1) ? 1.0f : 0.0f;
    output.texcoord.y = (input.VertexID & 2) ? 1.0f : 0.0f;

    output.alpha = float2(1.0f, 0.0f);

    return output;
}

float4 ps_blitter(VSOutput input) : SV_TARGET
{
    return float4(t.Sample(s, input.texcoord).rgb, input.alpha.x);
}
