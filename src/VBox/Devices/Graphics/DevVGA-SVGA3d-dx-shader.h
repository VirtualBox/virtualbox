/* $Id: DevVGA-SVGA3d-dx-shader.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevVGA - VMWare SVGA device - VGPU10+ (DX) shader utilities.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Graphics_DevVGA_SVGA3d_dx_shader_h
#define VBOX_INCLUDED_SRC_Graphics_DevVGA_SVGA3d_dx_shader_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifndef VMSVGA3D_DX
# error "This include file is for VMSVGA3D_DX."
#endif

#include <iprt/types.h>

#include "vmsvga_headers_begin.h"
#pragma pack(1) /* VMSVGA structures are '__packed'. */
#include <svga3d_reg.h>
#include <VGPU10ShaderTokens.h>
#pragma pack()
#include "vmsvga_headers_end.h"

/* SVGA3dDXSignatureRegisterComponentType (D3D10_SB_REGISTER_COMPONENT_TYPE) */
#define SVGADX_SIGNATURE_REGISTER_COMPONENT_UINT32  1
#define SVGADX_SIGNATURE_REGISTER_COMPONENT_SINT32  2
#define SVGADX_SIGNATURE_REGISTER_COMPONENT_FLOAT32 3

typedef struct DXShaderAttributeSemantic
{
    const char *pcszSemanticName;
    uint32_t SemanticIndex;
} DXShaderAttributeSemantic;

typedef struct DXShaderInfo
{
    VGPU10_PROGRAM_TYPE enmProgramType;
    bool fGuestSignatures : 1;
    void *pvBytecode;
    uint32_t cbBytecode;
    uint32_t cInputSignature;
    uint32_t cOutputSignature;
    uint32_t cPatchConstantSignature;
    uint32_t cDclResource;
    SVGA3dDXSignatureEntry aInputSignature[32];
    SVGA3dDXSignatureEntry aOutputSignature[32];
    SVGA3dDXSignatureEntry aPatchConstantSignature[32];
    DXShaderAttributeSemantic aInputSemantic[32];
    DXShaderAttributeSemantic aOutputSemantic[32];
    DXShaderAttributeSemantic aPatchConstantSemantic[32];
    uint32_t aOffDclResource[SVGA3D_DX_MAX_SRVIEWS];
} DXShaderInfo;

int DXShaderParse(void const *pvCode, uint32_t cbCode, DXShaderInfo *pInfo);
void DXShaderGenerateSemantics(DXShaderInfo *pInfo);
void DXShaderSortSignatures(DXShaderInfo *pInfo);
void DXShaderFree(DXShaderInfo *pInfo);
int DXShaderUpdateResources(DXShaderInfo const *pInfo, VGPU10_RESOURCE_DIMENSION *paResourceDimension,
                            VGPU10_RESOURCE_RETURN_TYPE *paResourceReturnType, uint32_t cResources);
VGPU10_RESOURCE_RETURN_TYPE DXShaderResourceReturnTypeFromFormat(SVGA3dSurfaceFormat format);
SVGA3dDXSignatureRegisterComponentType DXShaderComponentTypeFromFormat(SVGA3dSurfaceFormat format);
int DXShaderCreateDXBC(DXShaderInfo const *pInfo, void **ppvDXBC, uint32_t *pcbDXBC);
char const *DXShaderGetOutputSemanticName(DXShaderInfo const *pInfo, uint32_t idxRegister, SVGA3dDXSignatureSemanticName *pSemanticName);

#endif /* !VBOX_INCLUDED_SRC_Graphics_DevVGA_SVGA3d_dx_shader_h */
