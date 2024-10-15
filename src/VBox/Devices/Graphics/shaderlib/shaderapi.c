/* $Id: shaderapi.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/assert.h>
#include <iprt/log.h>
#define WINED3D_EXTERN
#include "wined3d_private.h"

#include "shaderlib.h"

#ifdef RT_OS_WINDOWS
# define OGLGETPROCADDRESS      wglGetProcAddress

#elif RT_OS_DARWIN
# include <dlfcn.h>
# define OGLGETPROCADDRESS(x)   MyNSGLGetProcAddress((const char *)x)
void *MyNSGLGetProcAddress(const char *pszSymbol)
{
    /* Another copy in DevVGA-SVGA3d-ogl.cpp. */
    static void *s_pvImage = NULL;
    if (s_pvImage == NULL)
        s_pvImage = dlopen("/System/Library/Frameworks/OpenGL.framework/Versions/Current/OpenGL", RTLD_LAZY);
    return s_pvImage ? dlsym(s_pvImage, pszSymbol) : NULL;
}

#else
extern void (*glXGetProcAddress(const GLubyte *procname))( void );
# define OGLGETPROCADDRESS(x)   glXGetProcAddress((const GLubyte *)x)

#endif

#undef GL_EXT_FUNCS_GEN
#define GL_EXT_FUNCS_GEN \
    /* GL_ARB_shader_objects */ \
    USE_GL_FUNC(WINED3D_PFNGLGETOBJECTPARAMETERIVARBPROC, \
            glGetObjectParameterivARB,                  ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLGETOBJECTPARAMETERFVARBPROC, \
            glGetObjectParameterfvARB,                  ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLGETUNIFORMLOCATIONARBPROC, \
            glGetUniformLocationARB,                    ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLGETACTIVEUNIFORMARBPROC, \
            glGetActiveUniformARB,                      ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLUNIFORM1IARBPROC, \
            glUniform1iARB,                             ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLUNIFORM2IARBPROC, \
            glUniform2iARB,                             ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLUNIFORM3IARBPROC, \
            glUniform3iARB,                             ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLUNIFORM4IARBPROC, \
            glUniform4iARB,                             ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLUNIFORM1FARBPROC, \
            glUniform1fARB,                             ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLUNIFORM2FARBPROC, \
            glUniform2fARB,                             ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLUNIFORM3FARBPROC, \
            glUniform3fARB,                             ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLUNIFORM4FARBPROC, \
            glUniform4fARB,                             ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLUNIFORM1FVARBPROC, \
            glUniform1fvARB,                            ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLUNIFORM2FVARBPROC, \
            glUniform2fvARB,                            ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLUNIFORM3FVARBPROC, \
            glUniform3fvARB,                            ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLUNIFORM4FVARBPROC, \
            glUniform4fvARB,                            ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLUNIFORM1IVARBPROC, \
            glUniform1ivARB,                            ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLUNIFORM2IVARBPROC, \
            glUniform2ivARB,                            ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLUNIFORM3IVARBPROC, \
            glUniform3ivARB,                            ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLUNIFORM4IVARBPROC, \
            glUniform4ivARB,                            ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLUNIFORMMATRIX2FVARBPROC, \
            glUniformMatrix2fvARB,                      ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLUNIFORMMATRIX3FVARBPROC, \
            glUniformMatrix3fvARB,                      ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLUNIFORMMATRIX4FVARBPROC, \
            glUniformMatrix4fvARB,                      ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLGETUNIFORMFVARBPROC, \
            glGetUniformfvARB,                          ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLGETUNIFORMIVARBPROC, \
            glGetUniformivARB,                          ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLGETINFOLOGARBPROC, \
            glGetInfoLogARB,                            ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLUSEPROGRAMOBJECTARBPROC, \
            glUseProgramObjectARB,                      ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLCREATESHADEROBJECTARBPROC, \
            glCreateShaderObjectARB,                    ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLSHADERSOURCEARBPROC, \
            glShaderSourceARB,                          ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLCOMPILESHADERARBPROC, \
            glCompileShaderARB,                         ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLCREATEPROGRAMOBJECTARBPROC, \
            glCreateProgramObjectARB,                   ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLATTACHOBJECTARBPROC, \
            glAttachObjectARB,                          ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLLINKPROGRAMARBPROC, \
            glLinkProgramARB,                           ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLDETACHOBJECTARBPROC, \
            glDetachObjectARB,                          ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLDELETEOBJECTARBPROC, \
            glDeleteObjectARB,                          ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLVALIDATEPROGRAMARBPROC, \
            glValidateProgramARB,                       ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLGETATTACHEDOBJECTSARBPROC, \
            glGetAttachedObjectsARB,                    ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLGETHANDLEARBPROC, \
            glGetHandleARB,                             ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLGETSHADERSOURCEARBPROC, \
            glGetShaderSourceARB,                       ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLBINDATTRIBLOCATIONARBPROC, \
            glBindAttribLocationARB,                    ARB_SHADER_OBJECTS,             NULL) \
    USE_GL_FUNC(WINED3D_PFNGLGETATTRIBLOCATIONARBPROC, \
            glGetAttribLocationARB,                     ARB_SHADER_OBJECTS,             NULL) \

static struct wined3d_context *g_pCurrentContext = NULL;
static struct wined3d_adapter g_adapter = {0};
static bool g_fInitializedLibrary = false;

#define SHADER_SET_CURRENT_CONTEXT(ctx) \
    g_pCurrentContext = (struct wined3d_context *)ctx;

SHADERDECL(int) ShaderInitLib(PVBOXVMSVGASHADERIF pVBoxShaderIf)
{
    struct wined3d_gl_info *gl_info = &g_adapter.gl_info;

    /* Dynamically load all GL core functions. */
#ifdef RT_OS_WINDOWS
    HANDLE hOpenGl32 = GetModuleHandle("opengl32.dll");
# define USE_GL_FUNC(pfn) *(FARPROC *)(&pfn) = GetProcAddress(hOpenGl32, #pfn);
#else
# define USE_GL_FUNC(pfn) pfn = (void *)OGLGETPROCADDRESS(#pfn);
#endif
    GL_FUNCS_GEN;
#undef USE_GL_FUNC

    /* Dynamically load all GL extension functions. */
#define USE_GL_FUNC(type, pfn, ext, replace) \
{ \
    gl_info->pfn = (type)OGLGETPROCADDRESS(#pfn); \
}
    GL_EXT_FUNCS_GEN;

    /* Fill in GL capabilities. */
    IWineD3DImpl_FillGLCaps(&g_adapter, pVBoxShaderIf);

    LogRel(("shaderlib: GL Limits:\n"));
    LogRel(("shaderlib:   buffers=%-2u                lights=%-2u                    textures=%-2u            texture_stages=%u\n",
            gl_info->limits.buffers, gl_info->limits.lights, gl_info->limits.textures, gl_info->limits.texture_stages));
    LogRel(("shaderlib:   fragment_samplers=%-2u      vertex_samplers=%-2u           combined_samplers=%-3u  general_combiners=%u\n",
            gl_info->limits.fragment_samplers, gl_info->limits.vertex_samplers, gl_info->limits.combined_samplers, gl_info->limits.general_combiners));
    LogRel(("shaderlib:   sampler_stages=%-2u         clipplanes=%-2u                texture_size=%-5u     texture3d_size=%u\n",
            gl_info->limits.sampler_stages, gl_info->limits.clipplanes, gl_info->limits.texture_size, gl_info->limits.texture3d_size));
    LogRel(("shaderlib:   pointsize_max=%d.%d      pointsize_min=%d.%d            point_sprite_units=%-2u  blends=%u\n",
            (int)gl_info->limits.pointsize_max, (int)(gl_info->limits.pointsize_max * 10) % 10,
            (int)gl_info->limits.pointsize_min, (int)(gl_info->limits.pointsize_min * 10) % 10,
            gl_info->limits.point_sprite_units, gl_info->limits.blends));
    LogRel(("shaderlib:   anisotropy=%-2u             shininess=%d.%02d\n",
            gl_info->limits.anisotropy, (int)gl_info->limits.shininess, (int)(gl_info->limits.shininess * 100) % 100));
    LogRel(("shaderlib:   glsl_varyings=%-3u         glsl_vs_float_constants=%-4u glsl_ps_float_constants=%u\n",
            gl_info->limits.glsl_varyings, gl_info->limits.glsl_vs_float_constants, gl_info->limits.glsl_ps_float_constants));
    LogRel(("shaderlib:   arb_vs_instructions=%-4u  arb_vs_native_constants=%-4u qarb_vs_float_constants=%u\n",
            gl_info->limits.arb_vs_instructions, gl_info->limits.arb_vs_native_constants, gl_info->limits.arb_vs_float_constants));
    LogRel(("shaderlib:   arb_vs_temps=%-2u           arb_ps_float_constants=%-4u  arb_ps_local_constants=%u\n",
            gl_info->limits.arb_vs_temps, gl_info->limits.arb_ps_float_constants, gl_info->limits.arb_ps_local_constants));
    LogRel(("shaderlib:   arb_ps_instructions=%-4u  arb_ps_temps=%-2u              arb_ps_native_constants=%u\n",
            gl_info->limits.arb_ps_instructions, gl_info->limits.arb_ps_temps, gl_info->limits.arb_ps_native_constants));

    g_fInitializedLibrary = true;
    return VINF_SUCCESS;
}

SHADERDECL(int) ShaderDestroyLib(void)
{
    return VINF_SUCCESS;
}

struct IWineD3DDeviceImpl *context_get_device(const struct wined3d_context *context)
{
    return context->pDeviceContext;
}

struct wined3d_context *context_get_current(void)
{
    return g_pCurrentContext;
}

struct wined3d_context *context_acquire(IWineD3DDeviceImpl *This, IWineD3DSurface *target, enum ContextUsage usage)
{
    RT_NOREF(This, target, usage);
    return g_pCurrentContext;
}

SHADERDECL(int) ShaderContextCreate(void **ppShaderContext)
{
    struct wined3d_context *pContext;
    HRESULT hr;

    pContext = (struct wined3d_context *)RTMemAllocZ(sizeof(struct wined3d_context));
    AssertReturn(pContext, VERR_NO_MEMORY);
    pContext->pDeviceContext = (IWineD3DDeviceImpl *)RTMemAllocZ(sizeof(IWineD3DDeviceImpl));
    AssertReturn(pContext->pDeviceContext, VERR_NO_MEMORY);

    pContext->gl_info = &g_adapter.gl_info;

    pContext->pDeviceContext->adapter = &g_adapter;
    pContext->pDeviceContext->shader_backend = &glsl_shader_backend;
    pContext->pDeviceContext->ps_selected_mode = SHADER_GLSL;
    pContext->pDeviceContext->vs_selected_mode = SHADER_GLSL;
#ifndef VBOX_WITH_VMSVGA
    pContext->render_offscreen = false;
#else
    /* VMSVGA always renders offscreen. */
    pContext->render_offscreen = true;
#endif

    list_init(&pContext->pDeviceContext->shaders);

    if (g_fInitializedLibrary)
    {
        struct shader_caps shader_caps;
        uint32_t state;

        /* Initialize the shader backend. */
        hr = pContext->pDeviceContext->shader_backend->shader_alloc_private((IWineD3DDevice *)pContext->pDeviceContext);
        AssertReturn(hr == S_OK, VERR_INTERNAL_ERROR);

        memset(&shader_caps, 0, sizeof(shader_caps));
        pContext->pDeviceContext->shader_backend->shader_get_caps(&g_adapter.gl_info, &shader_caps);
        pContext->pDeviceContext->d3d_vshader_constantF = shader_caps.MaxVertexShaderConst;
        pContext->pDeviceContext->d3d_pshader_constantF = shader_caps.MaxPixelShaderConst;
        pContext->pDeviceContext->vs_clipping = shader_caps.VSClipping;

        pContext->pDeviceContext->stateBlock = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*pContext->pDeviceContext->stateBlock));
        AssertReturn(pContext->pDeviceContext->stateBlock, VERR_NO_MEMORY);
        hr = stateblock_init(pContext->pDeviceContext->stateBlock, pContext->pDeviceContext, 0);
        AssertReturn(hr == S_OK, VERR_INTERNAL_ERROR);
        pContext->pDeviceContext->updateStateBlock = pContext->pDeviceContext->stateBlock;

        pContext->pDeviceContext->stateBlock->vertexDecl = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IWineD3DVertexDeclarationImpl));
        AssertReturn(pContext->pDeviceContext->stateBlock->vertexDecl, VERR_NO_MEMORY);

        /* Initialize the texture unit mapping to a 1:1 mapping */
        for (state = 0; state < MAX_COMBINED_SAMPLERS; ++state)
        {
            if (state < pContext->gl_info->limits.fragment_samplers)
            {
                pContext->pDeviceContext->texUnitMap[state] = state;
                pContext->pDeviceContext->rev_tex_unit_map[state] = state;
            } else {
                pContext->pDeviceContext->texUnitMap[state] = WINED3D_UNMAPPED_STAGE;
                pContext->pDeviceContext->rev_tex_unit_map[state] = WINED3D_UNMAPPED_STAGE;
            }
        }
    }

    *ppShaderContext = (void *)pContext;
    return VINF_SUCCESS;
}

SHADERDECL(int) ShaderContextDestroy(void *pShaderContext)
{
    struct wined3d_context *pContext = (struct wined3d_context *)pShaderContext;

    if (pContext->pDeviceContext)
    {
        IWineD3DStateBlockImpl *This = pContext->pDeviceContext->stateBlock;

        /* Fails during init only. */
        if (pContext->pDeviceContext->shader_priv)
            pContext->pDeviceContext->shader_backend->shader_free_private((IWineD3DDevice *)pContext->pDeviceContext);

        if (This)
        {
            if (This->vertexShaderConstantF)
                HeapFree(GetProcessHeap(), 0, This->vertexShaderConstantF);
            if (This->changed.vertexShaderConstantsF)
                HeapFree(GetProcessHeap(), 0, This->changed.vertexShaderConstantsF);
            if (This->pixelShaderConstantF)
                HeapFree(GetProcessHeap(), 0, This->pixelShaderConstantF);
            if (This->changed.pixelShaderConstantsF)
                HeapFree(GetProcessHeap(), 0, This->changed.pixelShaderConstantsF);
            if (This->contained_vs_consts_f)
                HeapFree(GetProcessHeap(), 0, This->contained_vs_consts_f);
            if (This->contained_ps_consts_f)
                HeapFree(GetProcessHeap(), 0, This->contained_ps_consts_f);
            if (This->vertexDecl)
                HeapFree(GetProcessHeap(), 0, This->vertexDecl);
            HeapFree(GetProcessHeap(), 0, This);
        }

        RTMemFree(pContext->pDeviceContext);
    }
    RTMemFree(pShaderContext);
    return VINF_SUCCESS;
}

SHADERDECL(int) ShaderCreateVertexShader(void *pShaderContext, const uint32_t *pShaderData, uint32_t cbShaderData, void **pShaderObj)
{
    IWineD3DDeviceImpl *This;
    IWineD3DVertexShaderImpl *object;
    HRESULT hr;

    SHADER_SET_CURRENT_CONTEXT(pShaderContext);
    This = g_pCurrentContext->pDeviceContext;

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
    {
        Log(("Failed to allocate shader memory.\n"));
        return VERR_NO_MEMORY;
    }

    object->baseShader.functionLength = cbShaderData;

    hr = vertexshader_init(object, This, (DWORD const *)pShaderData, NULL, NULL, NULL);
    if (FAILED(hr))
    {
        Log(("Failed to initialize vertex shader, hr %#x.\n", hr));
        HeapFree(GetProcessHeap(), 0, object);
        return VERR_INTERNAL_ERROR;
    }

    /* Tweak the float constants limit to use a greater number of constants.
     * Keep some space for the internal usage.
     * The shader creation code artificially sets the limit according to D3D shader version.
     * But the guest may use more constants and we are not required to strictly follow D3D specs.
     */
    object->baseShader.limits.constant_float = RT_MAX(g_adapter.gl_info.limits.glsl_vs_float_constants / 2,
                                                      object->baseShader.limits.constant_float);

#ifdef VBOX_WINE_WITH_SHADER_CACHE
    object = vertexshader_check_cached(This, object);
#endif

    Log(("Created vertex shader %p.\n", object));
    *pShaderObj = (void *)object;

    return VINF_SUCCESS;
}

SHADERDECL(int) ShaderCreatePixelShader(void *pShaderContext, const uint32_t *pShaderData, uint32_t cbShaderData, void **pShaderObj)
{
    IWineD3DDeviceImpl *This;
    IWineD3DPixelShaderImpl *object;
    HRESULT hr;

    SHADER_SET_CURRENT_CONTEXT(pShaderContext);
    This = g_pCurrentContext->pDeviceContext;

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
    {
        Log(("Failed to allocate shader memory.\n"));
        return VERR_NO_MEMORY;
    }

    object->baseShader.functionLength = cbShaderData;

    hr = pixelshader_init(object, This, (DWORD const *)pShaderData, NULL, NULL, NULL);
    if (FAILED(hr))
    {
        Log(("Failed to initialize pixel shader, hr %#x.\n", hr));
        HeapFree(GetProcessHeap(), 0, object);
        return VERR_INTERNAL_ERROR;
    }

    /* Tweak the float constants limit to use a greater number of constants.
     * Keep some space for the internal usage.
     * The shader creation code artificially sets the limit according to D3D shader version.
     * But the guest may use more constants and we are not required to strictly follow D3D specs.
     */
    object->baseShader.limits.constant_float = RT_MAX(g_adapter.gl_info.limits.glsl_ps_float_constants / 2,
                                                      object->baseShader.limits.constant_float);

#ifdef VBOX_WINE_WITH_SHADER_CACHE
    object = pixelshader_check_cached(This, object);
#endif

    Log(("Created pixel shader %p.\n", object));
    *pShaderObj = (void *)object;
    return VINF_SUCCESS;
}

SHADERDECL(int) ShaderDestroyVertexShader(void *pShaderContext, void *pShaderObj)
{
    IWineD3DVertexShaderImpl *object = (IWineD3DVertexShaderImpl *)pShaderObj;
    AssertReturn(pShaderObj, VERR_INVALID_PARAMETER);

    SHADER_SET_CURRENT_CONTEXT(pShaderContext);

    object->lpVtbl->Release((IWineD3DVertexShader *)object);
        return VINF_SUCCESS;
}

SHADERDECL(int) ShaderDestroyPixelShader(void *pShaderContext, void *pShaderObj)
{
    IWineD3DPixelShaderImpl *object = (IWineD3DPixelShaderImpl *)pShaderObj;
    AssertReturn(pShaderObj, VERR_INVALID_PARAMETER);

    SHADER_SET_CURRENT_CONTEXT(pShaderContext);

    object->lpVtbl->Release((IWineD3DPixelShader *)object);
        return VINF_SUCCESS;
}

SHADERDECL(int) ShaderSetVertexShader(void *pShaderContext, void *pShaderObj)
{
    IWineD3DDeviceImpl *This;
    IWineD3DVertexShader* pShader;
    IWineD3DVertexShader* oldShader;

    SHADER_SET_CURRENT_CONTEXT(pShaderContext);
    This = g_pCurrentContext->pDeviceContext;
    pShader   = (IWineD3DVertexShader* )pShaderObj;
    oldShader = This->updateStateBlock->vertexShader;

    if(oldShader == pShader) {
        /* Checked here to allow proper stateblock recording */
        Log(("App is setting the old shader over, nothing to do\n"));
        return VINF_SUCCESS;
    }

    This->updateStateBlock->vertexShader         = pShader;
    This->updateStateBlock->changed.vertexShader = TRUE;

    Log(("(%p) : setting pShader(%p)\n", This, pShader));
    if(pShader) IWineD3DVertexShader_AddRef(pShader);
    if(oldShader) IWineD3DVertexShader_Release(oldShader);

    g_pCurrentContext->fChangedVertexShader = true;
    g_pCurrentContext->fChangedVertexShaderConstant = true;    /* force constant reload. */

    return VINF_SUCCESS;
}

SHADERDECL(int) ShaderSetPixelShader(void *pShaderContext, void *pShaderObj)
{
    IWineD3DDeviceImpl *This;
    IWineD3DPixelShader* pShader;
    IWineD3DPixelShader* oldShader;

    SHADER_SET_CURRENT_CONTEXT(pShaderContext);
    This = g_pCurrentContext->pDeviceContext;
    pShader   = (IWineD3DPixelShader* )pShaderObj;
    oldShader = This->updateStateBlock->pixelShader;

    if(oldShader == pShader) {
        /* Checked here to allow proper stateblock recording */
        Log(("App is setting the old shader over, nothing to do\n"));
        return VINF_SUCCESS;
    }

    This->updateStateBlock->pixelShader         = pShader;
    This->updateStateBlock->changed.pixelShader = TRUE;

    Log(("(%p) : setting pShader(%p)\n", This, pShader));
    if(pShader) IWineD3DPixelShader_AddRef(pShader);
    if(oldShader) IWineD3DPixelShader_Release(oldShader);

    g_pCurrentContext->fChangedPixelShader = true;
    g_pCurrentContext->fChangedPixelShaderConstant = true;    /* force constant reload. */
    return VINF_SUCCESS;
}

SHADERDECL(int) ShaderSetVertexShaderConstantB(void *pShaderContext, uint32_t start, const uint8_t *srcData, uint32_t count)
{
    IWineD3DDeviceImpl *This;
    unsigned int i, cnt = min(count, MAX_CONST_B - start);

    SHADER_SET_CURRENT_CONTEXT(pShaderContext);
    This = g_pCurrentContext->pDeviceContext;

    Log(("(ShaderSetVertexShaderConstantB %p, srcData %p, start %d, count %d)\n", pShaderContext, srcData, start, count));

    if (!srcData || start >= MAX_CONST_B)
    {
        Log(("incorrect vertex shader const data: start(%u), srcData(0x%p), count(%u)", start, srcData, count));
        return VERR_INVALID_PARAMETER;
    }

    memcpy(&This->updateStateBlock->vertexShaderConstantB[start], srcData, cnt * sizeof(BOOL));
    for (i = 0; i < cnt; i++)
        Log(("Set BOOL constant %u to %s\n", start + i, srcData[i]? "true":"false"));

    for (i = start; i < cnt + start; ++i) {
        This->updateStateBlock->changed.vertexShaderConstantsB |= (1 << i);
    }

    g_pCurrentContext->fChangedVertexShaderConstant = true;

    return VINF_SUCCESS;
}

SHADERDECL(int) ShaderSetVertexShaderConstantI(void *pShaderContext, uint32_t start, const int32_t *srcData, uint32_t count)
{
    IWineD3DDeviceImpl *This;
    unsigned int i, cnt = min(count, MAX_CONST_I - start);

    SHADER_SET_CURRENT_CONTEXT(pShaderContext);
    This = g_pCurrentContext->pDeviceContext;

    Log(("(ShaderSetVertexShaderConstantI %p, srcData %p, start %d, count %d)\n", pShaderContext, srcData, start, count));

    if (!srcData || start >= MAX_CONST_I)
    {
        Log(("incorrect vertex shader const data: start(%u), srcData(0x%p), count(%u)", start, srcData, count));
        return VERR_INVALID_PARAMETER;
    }

    memcpy(&This->updateStateBlock->vertexShaderConstantI[start * 4], srcData, cnt * sizeof(int32_t) * 4);

    for (i = start; i < cnt + start; ++i) {
        This->updateStateBlock->changed.vertexShaderConstantsI |= (1 << i);
    }

    g_pCurrentContext->fChangedVertexShaderConstant = true;

    return VINF_SUCCESS;
}

SHADERDECL(int) ShaderSetVertexShaderConstantF(void *pShaderContext, uint32_t start, const float *srcData, uint32_t count)
{
    IWineD3DDeviceImpl *This;

    SHADER_SET_CURRENT_CONTEXT(pShaderContext);
    This = g_pCurrentContext->pDeviceContext;

    Log(("(ShaderSetVertexShaderConstantF %p, srcData %p, start %d, count %d)\n", pShaderContext, srcData, start, count));

    if (srcData == NULL || start + count > This->d3d_vshader_constantF || start > This->d3d_vshader_constantF)
    {
        Log(("incorrect vertex shader const data: start(%u), srcData(0x%p), count(%u)", start, srcData, count));
        return VERR_INVALID_PARAMETER;
    }
    memcpy(&This->updateStateBlock->vertexShaderConstantF[start * 4], srcData, count * sizeof(float) * 4);

    This->shader_backend->shader_update_float_vertex_constants((IWineD3DDevice *)This, start, count);

    memset(This->updateStateBlock->changed.vertexShaderConstantsF + start, 1,
           sizeof(*This->updateStateBlock->changed.vertexShaderConstantsF) * count);

    g_pCurrentContext->fChangedVertexShaderConstant = true;

    return VINF_SUCCESS;
}

SHADERDECL(int) ShaderSetPixelShaderConstantB(void *pShaderContext, uint32_t start, const uint8_t *srcData, uint32_t count)
{
    IWineD3DDeviceImpl *This;
    unsigned int i, cnt = min(count, MAX_CONST_B - start);

    SHADER_SET_CURRENT_CONTEXT(pShaderContext);
    This = g_pCurrentContext->pDeviceContext;

    Log(("(ShaderSetPixelShaderConstantB %p, srcData %p, start %d, count %d)\n", pShaderContext, srcData, start, count));

    if (!srcData || start >= MAX_CONST_B)
    {
        Log(("incorrect pixel shader const data: start(%u), srcData(0x%p), count(%u)", start, srcData, count));
        return VERR_INVALID_PARAMETER;
    }

    memcpy(&This->updateStateBlock->pixelShaderConstantB[start], srcData, cnt * sizeof(BOOL));
    for (i = 0; i < cnt; i++)
        Log(("Set BOOL constant %u to %s\n", start + i, srcData[i]? "true":"false"));

    for (i = start; i < cnt + start; ++i) {
        This->updateStateBlock->changed.pixelShaderConstantsB |= (1 << i);
    }

    g_pCurrentContext->fChangedPixelShaderConstant = true;

    return VINF_SUCCESS;
}

SHADERDECL(int) ShaderSetPixelShaderConstantI(void *pShaderContext, uint32_t start, const int32_t *srcData, uint32_t count)
{
    IWineD3DDeviceImpl *This;
    unsigned int i, cnt = min(count, MAX_CONST_I - start);

    SHADER_SET_CURRENT_CONTEXT(pShaderContext);
    This = g_pCurrentContext->pDeviceContext;

    Log(("(ShaderSetPixelShaderConstantI %p, srcData %p, start %d, count %d)\n", pShaderContext, srcData, start, count));

    if (!srcData || start >= MAX_CONST_I)
    {
        Log(("incorrect pixel shader const data: start(%u), srcData(0x%p), count(%u)", start, srcData, count));
        return VERR_INVALID_PARAMETER;
    }

    memcpy(&This->updateStateBlock->pixelShaderConstantI[start * 4], srcData, cnt * sizeof(int32_t) * 4);

    for (i = start; i < cnt + start; ++i) {
        This->updateStateBlock->changed.pixelShaderConstantsI |= (1 << i);
    }

    g_pCurrentContext->fChangedPixelShaderConstant = true;

    return VINF_SUCCESS;
}

SHADERDECL(int) ShaderSetPixelShaderConstantF(void *pShaderContext, uint32_t start, const float *srcData, uint32_t count)
{
    IWineD3DDeviceImpl *This;

    SHADER_SET_CURRENT_CONTEXT(pShaderContext);
    This = g_pCurrentContext->pDeviceContext;

    Log(("(ShaderSetPixelShaderConstantF %p, srcData %p, start %d, count %d)\n", pShaderContext, srcData, start, count));

    if (srcData == NULL || start + count > This->d3d_pshader_constantF || start > This->d3d_pshader_constantF)
    {
        Log(("incorrect pixel shader const data: start(%u), srcData(0x%p), count(%u)", start, srcData, count));
        return VERR_INVALID_PARAMETER;
    }

    memcpy(&This->updateStateBlock->pixelShaderConstantF[start * 4], srcData, count * sizeof(float) * 4);

    This->shader_backend->shader_update_float_pixel_constants((IWineD3DDevice *)This, start, count);

    memset(This->updateStateBlock->changed.pixelShaderConstantsF + start, 1,
            sizeof(*This->updateStateBlock->changed.pixelShaderConstantsF) * count);

    g_pCurrentContext->fChangedPixelShaderConstant = true;

    return VINF_SUCCESS;
}

SHADERDECL(int) ShaderSetPositionTransformed(void *pShaderContext, unsigned cxViewPort, unsigned cyViewPort, bool fPreTransformed)
{
    IWineD3DDeviceImpl *This;
    int rc;

    SHADER_SET_CURRENT_CONTEXT(pShaderContext);
    This = g_pCurrentContext->pDeviceContext;

    if (This->strided_streams.position_transformed == fPreTransformed)
        return VINF_SUCCESS;    /* no changes; nothing to do. */

    Log(("ShaderSetPositionTransformed viewport (%d,%d) fPreTransformed=%d\n", cxViewPort, cyViewPort, fPreTransformed));

    if (fPreTransformed)
    {   /* In the pre-transformed vertex coordinate case we need to disable all transformations as we're already using screen coordinates. */
        /* Load the identity matrix for the model view */
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        /* Reset the projection matrix too */
        rc = ShaderTransformProjection(cxViewPort, cyViewPort, NULL, fPreTransformed);
        AssertRCReturn(rc, rc);
    }

    This->strided_streams.position_transformed = fPreTransformed;
    ((IWineD3DVertexDeclarationImpl *)(This->stateBlock->vertexDecl))->position_transformed = fPreTransformed;
    return VINF_SUCCESS;
}

SHADERDECL(int) ShaderUpdateState(void *pShaderContext, uint32_t rtHeight)
{
    IWineD3DDeviceImpl *pThis;
    GLfloat yoffset;
    GLint viewport[4];

    SHADER_SET_CURRENT_CONTEXT(pShaderContext);
    pThis = g_pCurrentContext->pDeviceContext;

    glGetIntegerv(GL_VIEWPORT, viewport);
#ifdef DEBUG
    AssertReturn(glGetError() == GL_NO_ERROR, VERR_INTERNAL_ERROR);
#endif

    yoffset = -(63.0f / 64.0f) / viewport[3] /* height */;
    pThis->posFixup[0] = 1.0f;  /* This is needed to get the x coord unmodified through a MAD. */
    pThis->posFixup[1] = -1.0f;  /* y-inversion */
    pThis->posFixup[2] = (63.0f / 64.0f) / viewport[2] /* width */;
    pThis->posFixup[3] = pThis->posFixup[1] * yoffset;

    pThis->rtHeight = rtHeight;

    /** @todo missing state:
     * - fog enable (stateblock->renderState[WINED3DRS_FOGENABLE])
     * - fog mode (stateblock->renderState[WINED3DRS_FOGTABLEMODE])
     * - stateblock->vertexDecl->position_transformed
     */

    if (    g_pCurrentContext->fChangedPixelShader
        ||  g_pCurrentContext->fChangedVertexShader)
        pThis->shader_backend->shader_select(g_pCurrentContext, !!pThis->updateStateBlock->pixelShader, !!pThis->updateStateBlock->vertexShader);
    g_pCurrentContext->fChangedPixelShader = g_pCurrentContext->fChangedVertexShader = false;

    if (    g_pCurrentContext->fChangedPixelShaderConstant
        ||  g_pCurrentContext->fChangedVertexShaderConstant)
        pThis->shader_backend->shader_load_constants(g_pCurrentContext, !!pThis->updateStateBlock->pixelShader, !!pThis->updateStateBlock->vertexShader);
    g_pCurrentContext->fChangedPixelShaderConstant  = false;
    g_pCurrentContext->fChangedVertexShaderConstant = false;

    return VINF_SUCCESS;
}

SHADERDECL(int) ShaderTransformProjection(unsigned cxViewPort, unsigned cyViewPort, float matrix[16], bool fPretransformed)
{
#ifdef DEBUG
    GLenum lastError;
#endif
    GLfloat xoffset, yoffset;

    /* Assumes OpenGL context has been activated. */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    /* The rule is that the window coordinate 0 does not correspond to the
       beginning of the first pixel, but the center of the first pixel.
       As a consequence if you want to correctly draw one line exactly from
       the left to the right end of the viewport (with all matrices set to
       be identity), the x coords of both ends of the line would be not
       -1 and 1 respectively but (-1-1/viewport_widh) and (1-1/viewport_width)
       instead.

       1.0 / Width is used because the coord range goes from -1.0 to 1.0, then we
       divide by the Width/Height, so we need the half range(1.0) to translate by
       half a pixel.

       The other fun is that d3d's output z range after the transformation is [0;1],
       but opengl's is [-1;1]. Since the z buffer is in range [0;1] for both, gl
       scales [-1;1] to [0;1]. This would mean that we end up in [0.5;1] and loose a lot
       of Z buffer precision and the clear values do not match in the z test. Thus scale
       [0;1] to [-1;1], so when gl undoes that we utilize the full z range
    */

    /*
     * Careful with the order of operations here, we're essentially working backwards:
     * x = x + 1/w;
     * y = (y - 1/h) * flip;
     * z = z * 2 - 1;
     *
     * Becomes:
     * glTranslatef(0.0, 0.0, -1.0);
     * glScalef(1.0, 1.0, 2.0);
     *
     * glScalef(1.0, flip, 1.0);
     * glTranslatef(1/w, -1/h, 0.0);
     *
     * This is equivalent to:
     * glTranslatef(1/w, -flip/h, -1.0)
     * glScalef(1.0, flip, 2.0);
     */
    /* Translate by slightly less than a half pixel to force a top-left
     * filling convention. We want the difference to be large enough that
     * it doesn't get lost due to rounding inside the driver, but small
     * enough to prevent it from interfering with any anti-aliasing. */
    xoffset = (63.0f / 64.0f) / cxViewPort;
    yoffset = -(63.0f / 64.0f) / cyViewPort;

    glTranslatef(xoffset, -yoffset, -1.0f);

    if (fPretransformed)
    {
        /* One world coordinate equals one screen pixel; y-inversion no longer an issue */
        glOrtho(0, cxViewPort, 0, cyViewPort, -1, 1);
    }
    else
    {
        /* flip y coordinate origin too */
        glScalef(1.0f, -1.0f, 2.0f);

        /* Apply the supplied projection matrix */
        glMultMatrixf(matrix);
    }
#ifdef DEBUG
    lastError = glGetError();                                     \
    AssertMsgReturn(lastError == GL_NO_ERROR, ("%s (%d): last error 0x%x\n", __FUNCTION__, __LINE__, lastError), VERR_INTERNAL_ERROR);
#endif
    return VINF_SUCCESS;
}
