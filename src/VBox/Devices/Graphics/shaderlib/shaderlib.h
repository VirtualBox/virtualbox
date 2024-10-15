/* $Id: shaderlib.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * shaderlib -- interface to WINE's Direct3D shader functions
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Graphics_shaderlib_shaderlib_h
#define VBOX_INCLUDED_SRC_Graphics_shaderlib_shaderlib_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>

RT_C_DECLS_BEGIN

#ifdef IN_SHADERLIB_STATIC
# define SHADERDECL(type)           DECLHIDDEN(type) RTCALL
#else
# define SHADERDECL(type)           DECLEXPORT(type) RTCALL
#endif

/** Pointer to shaderlib callback interface. */
typedef struct VBOXVMSVGASHADERIF *PVBOXVMSVGASHADERIF;
/**
 * Interface the shader lib can use to talk back to the VBox VMSVGA OGL 3D code.
 */
typedef struct VBOXVMSVGASHADERIF
{
    /**
     * Switches the initialization profile in builds where we have to juggle two
     * OpenGL profiles to gather all the data (i.e. mac os x).
     *
     * @param   pThis           Pointer to this structure.
     * @param   fOtherProfile   If set, switch to the non-default profile.  If
     *                          clear, switch back to the default profile.
     */
    DECLCALLBACKMEMBER(void, pfnSwitchInitProfile,(PVBOXVMSVGASHADERIF pThis, bool fOtherProfile));

    /**
     * Extension enumeration function.
     *
     * @param   pThis           Pointer to this structure.
     * @param   ppvEnumCtx      Pointer to a void point that's initialized to NULL
     *                          before the first call.
     * @param   pszBuf          Where to store the extension name. Garbled on
     *                          overflow (we assume no overflow).
     * @param   cbBuf           The size of the buffer @a pszBuf points to.
     * @param   fOtherProfile   Indicates which profile to get extensions from,
     *                          we'll use the default profile if CLEAR and the
     *                          non-default if SET.
     */
    DECLCALLBACKMEMBER(bool, pfnGetNextExtension,(PVBOXVMSVGASHADERIF pThis, void **ppvEnumCtx, char *pszBuf, size_t cbBuf,
                                                  bool fOtherProfile));
} VBOXVMSVGASHADERIF;

SHADERDECL(int) ShaderInitLib(PVBOXVMSVGASHADERIF pVBoxShaderIf);
SHADERDECL(int) ShaderDestroyLib(void);

SHADERDECL(int) ShaderContextCreate(void **ppShaderContext);
SHADERDECL(int) ShaderContextDestroy(void *pShaderContext);

SHADERDECL(int) ShaderCreateVertexShader(void *pShaderContext, const uint32_t *pShaderData, uint32_t cbShaderData, void **pShaderObj);
SHADERDECL(int) ShaderCreatePixelShader(void *pShaderContext, const uint32_t *pShaderData, uint32_t cbShaderData, void **pShaderObj);

SHADERDECL(int) ShaderDestroyVertexShader(void *pShaderContext, void *pShaderObj);
SHADERDECL(int) ShaderDestroyPixelShader(void *pShaderContext, void *pShaderObj);

SHADERDECL(int) ShaderSetVertexShader(void *pShaderContext, void *pShaderObj);
SHADERDECL(int) ShaderSetPixelShader(void *pShaderContext, void *pShaderObj);

SHADERDECL(int) ShaderSetVertexShaderConstantB(void *pShaderContext, uint32_t reg, const uint8_t *pValues, uint32_t cRegisters);
SHADERDECL(int) ShaderSetVertexShaderConstantI(void *pShaderContext, uint32_t reg, const int32_t *pValues, uint32_t cRegisters);
SHADERDECL(int) ShaderSetVertexShaderConstantF(void *pShaderContext, uint32_t reg, const float *pValues, uint32_t cRegisters);

SHADERDECL(int) ShaderSetPixelShaderConstantB(void *pShaderContext, uint32_t reg, const uint8_t *pValues, uint32_t cRegisters);
SHADERDECL(int) ShaderSetPixelShaderConstantI(void *pShaderContext, uint32_t reg, const int32_t *pValues, uint32_t cRegisters);
SHADERDECL(int) ShaderSetPixelShaderConstantF(void *pShaderContext, uint32_t reg, const float *pValues, uint32_t cRegisters);

SHADERDECL(int) ShaderSetPositionTransformed(void *pShaderContext, unsigned cxViewPort, unsigned cyViewPort, bool fPreTransformed);

SHADERDECL(int) ShaderUpdateState(void *pShaderContext, uint32_t rtHeight);

SHADERDECL(int) ShaderTransformProjection(unsigned cxViewPort, unsigned cyViewPort, float matrix[16], bool fPretransformed);

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_Graphics_shaderlib_shaderlib_h */

