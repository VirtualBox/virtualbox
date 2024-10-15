/* $Id: DevVGA-SVGA3d-glHlp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevVMWare - VMWare SVGA device OpenGL backend
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#ifdef DEBUG_bird
//# define RTMEM_WRAP_TO_EF_APIS
#endif
#define LOG_GROUP LOG_GROUP_DEV_VMSVGA
#include <VBox/vmm/pdmdev.h>

#include <iprt/assert.h>
#include <iprt/mem.h>

#include "DevVGA-SVGA.h"
#include "DevVGA-SVGA3d-internal.h"

/* Parameters for glVertexAttribPointer. */
typedef struct VertexAttribDesc
{
    GLint     size;
    GLenum    type;
    GLboolean normalized;
    GLsizei   stride;
    GLsizei   offset;
} VertexAttribDesc;

/* Information about a shader program. */
typedef struct ShaderProgram
{
    GLuint vertexShader;   /* Vertex shader name. */
    GLuint fragmentShader; /* Fragment shader name. */
    GLuint program;        /* Shader program name. */
    GLint sSourceTex;      /* Location of the texture sampler uniform in the shader. */
    GLint uTexInfo;        /* Location of the texture information uniform in the shader. */
} ShaderProgram;

/* Texture format conversion data.
 * Uses a fragment (pixel) shader to render a source texture in one format
 * to the target texture in another format.
 */
typedef struct VMSVGA3DFORMATCONVERTER
{
    PVMSVGA3DSTATE pState;

    ShaderProgram programYUY2ToRGB;         /* From the YUY2 emulated format to the actual RGB texture. */
    ShaderProgram programYUY2FromRGB;       /* From the actual RGB texture to the emulated YUY2 format. */
    ShaderProgram programUYVYToRGB;         /* From the UYVY emulated format to the actual RGB texture. */
    ShaderProgram programUYVYFromRGB;       /* From the actual RGB texture to the emulated UYVY format. */

    GLuint framebuffer;                     /* Framebuffer object name. */

    GLuint vertexBuffer;                    /* Vertex attribute buffer. Position + texcoord. */
} VMSVGA3DFORMATCONVERTER;

/* Parameters for glVertexAttribPointer. */
static const VertexAttribDesc aVertexAttribs[] =
{
    {2, GL_FLOAT, GL_FALSE, 16, 0 }, /* Position. */
    {2, GL_FLOAT, GL_FALSE, 16, 8 }  /* Texcoord. */
};

/* Triangle fan */
static float const aAttribData[] =
{
    /* positions      texcoords */
    -1.0f,  -1.0f,    0.0f,   0.0f,
     1.0f,  -1.0f,    1.0f,   0.0f,
     1.0f,   1.0f,    1.0f,   1.0f,
    -1.0f,   1.0f,    0.0f,   1.0f
};

static const GLchar shaderHeaderSource[] =
{
    "  #version 120\n"
};

static const GLchar vertexShaderSource[] =
{
    "  attribute vec2 attrib0;\n"
    "  attribute vec2 attrib1;\n"
    "  void main(void)\n"
    "  {\n"
    "      gl_TexCoord[0].xy = attrib1;\n"
    "      gl_Position = vec4(attrib0.x, attrib0.y, 0.0f, 1.0f);\n"
    "  }\n"
};

static const GLchar fetchYUY2Source[] =
{
    "  vec4 fetchYUV(vec4 texColor)\n"
    "  {\n"
    "      return vec4(texColor.b, texColor.g, texColor.r, texColor.a);\n"
    "  }\n"
};

static const GLchar fetchUYVYSource[] =
{
    "  vec4 fetchYUV(vec4 texColor)\n"
    "  {\n"
    "      return vec4(texColor.g, texColor.b, texColor.a, texColor.r);\n"
    "  }\n"
};

static const GLchar YUV2RGBShaderSource[] =
{
    "  uniform sampler2D sSourceTex;\n"
    "  uniform vec4 uTexInfo;\n"
    "  \n"
    "  const mat3 yuvCoeffs = mat3\n"
    "  (\n"
    "      1.164383f,       0.0f,  1.596027f, // first column \n"
    "      1.164383f, -0.391762f, -0.812968f, // second column\n"
    "      1.164383f,  2.017232f,  0.0f       // third column\n"
    "  );\n"
    "  \n"
    "  void main() {\n"
    "      // Input texcoords are in [0;1] range for the target.\n"
    "      vec2 texCoord = gl_TexCoord[0].xy;\n"
    "      // Convert to the target coords in pixels: xPixel = texCoord.x * TextureWidth. \n"
    "      float xTargetPixel = texCoord.x * uTexInfo.x;\n"
    "      // Source texture is half width, i.e. it contains data in pixels [0; width / 2 - 1].\n"
    "      float xSourcePixel = xTargetPixel / 2.0f;\n"
    "      // Remainder is about 0.25 for even pixels and about 0.75 for odd pixels.\n"
    "      float remainder = fract(xSourcePixel);\n"
    "      // Back to the normalized coords: texCoord.x = xPixel / Width.\n"
    "      texCoord.x = xSourcePixel * uTexInfo.z;\n"
    "      vec4 texColor = texture2D(sSourceTex, texCoord);\n"
    "      vec4 y0uy1v = fetchYUV(texColor);\n"
    "      // Get y0 for even x coordinates and y1 for odd ones.\n"
    "      float y = remainder < 0.5f ? y0uy1v.x : y0uy1v.z;\n"
    "      // Make a vector for easier calculation.\n"
    "      vec3 yuv = vec3(y, y0uy1v.y, y0uy1v.w);\n"
    "      yuv -= vec3(0.0627f, 0.502f, 0.502f);\n"
    "      vec3 bgr = yuv * yuvCoeffs;\n"
    "      //vec3 bgr;\n"
    "      //bgr.r = 1.164383 * yuv.x                    + 1.596027 * yuv.z;\n"
    "      //bgr.g = 1.164383 * yuv.x - 0.391762 * yuv.y - 0.812968 * yuv.z;\n"
    "      //bgr.b = 1.164383 * yuv.x + 2.017232 * yuv.y;\n"
    "      bgr = clamp(bgr, 0.0f, 1.0f);\n"
    "      gl_FragData[0] = vec4(bgr, 1.0f);\n"
    "  }\n"
};

static const GLchar storeYUY2Source[] =
{
    "  vec4 storeYUV(float y0, float u, float y1, float v)\n"
    "  {\n"
    "      return vec4(y1, u, y0, v);\n"
    "  }\n"
};

static const GLchar storeUYVYSource[] =
{
    "  vec4 storeYUV(float y0, float u, float y1, float v)\n"
    "  {\n"
    "      return vec4(u, y1, v, y0);\n"
    "  }\n"
};

static const GLchar RGB2YUVShaderSource[] =
{
    "  uniform sampler2D sSourceTex;\n"
    "  uniform vec4 uTexInfo;\n"
    "  \n"
    "  const mat3 bgrCoeffs = mat3\n"
    "  (\n"
    "       0.2578f,  0.5039f,  0.0977f, // first column \n"
    "      -0.1484f, -0.2891f,  0.4375f, // second column\n"
    "       0.4375f, -0.3672f, -0.0703f  // third column\n"
    "  );\n"
    "  const vec3 yuvShift = vec3(0.0647f, 0.5039f, 0.5039f);\n"
    "  \n"
    "  void main() {\n"
    "      // Input texcoords are in [0;1] range for the target.\n"
    "      vec2 texCoordDst = gl_TexCoord[0].xy;\n"
    "      // Convert to the target coords in pixels: xPixel = TexCoord.x * TextureWidth.\n"
    "      float xTargetPixel = texCoordDst.x * uTexInfo.x;\n"
    "      vec4 bgraOutputPixel;\n"
    "      if (xTargetPixel < uTexInfo.x / 2.0f)\n"
    "      {\n"
    "          // Target texture is half width, i.e. it contains data in pixels [0; width / 2 - 1].\n"
    "          // Compute the source texture coords for the pixels which will be used to compute the target pixel.\n"
    "          vec2 texCoordSrc = texCoordDst;\n"
    "          texCoordSrc.x *= 2.0f;\n"
    "          // Even pixel. Fetch two BGRA source pixels.\n"
    "          vec4 texColor0 = texture2D(sSourceTex, texCoordSrc);\n"
    "          // Advance one pixel (+ 1/Width)\n"
    "          texCoordSrc.x += uTexInfo.z;\n"
    "          vec4 texColor1 = texture2D(sSourceTex, texCoordSrc);\n"
    "          vec3 yuv0 = texColor0.rgb * bgrCoeffs;\n"
    "          yuv0 += yuvShift;\n"
    "          vec3 yuv1 = texColor1.rgb * bgrCoeffs;\n"
    "          yuv1 += yuvShift;\n"
    "          float y0 = yuv0.r;\n"
    "          float  u = (yuv0.g + yuv1.g) / 2.0f;\n"
    "          float y1 = yuv1.r;\n"
    "          float  v = (yuv0.b + yuv1.b) / 2.0f;\n"
    "          bgraOutputPixel = storeYUV(y0, u, y1, v);\n"
    "      }\n"
    "      else\n"
    "      {\n"
    "          // [width / 2; width - 1] pixels are not used. Set to something.\n"
    "          bgraOutputPixel = vec4(0.0f, 0.0f, 0.0f, 0.0f);\n"
    "      }\n"
    "      bgraOutputPixel = clamp(bgraOutputPixel, 0.0f, 1.0f);\n"
    "      gl_FragData[0] = bgraOutputPixel;\n"
    "  }\n"
};

#define GL_CHECK_ERROR() do { \
    VMSVGA3D_CHECK_LAST_ERROR_WARN(pState, pContext); \
    if (pContext->lastError != GL_NO_ERROR) \
        LogRelMax(10, ("VMSVGA: %s (%d): GL error 0x%x\n", __FUNCTION__, __LINE__, pContext->lastError)); \
} while (0)

/* Compile shaders and link a shader program. */
static void createShaderProgram(PVMSVGA3DSTATE pState,
                                ShaderProgram *pProgram,
                                int cVertexSources, const GLchar **apszVertexSources,
                                int cFragmentSources, const GLchar **apszFragmentSources)
{
    AssertReturnVoid(pState->idActiveContext == VMSVGA3D_SHARED_CTX_ID);

    /* Everything is done on the shared context. The pState and pContext are for GL_CHECK_ERROR macro. */
    PVMSVGA3DCONTEXT pContext = &pState->SharedCtx;

    int success;
    char szInfoLog[1024];

    /*
     * VERTEX shader.
     */
    pProgram->vertexShader = pState->ext.glCreateShader(GL_VERTEX_SHADER);
    GL_CHECK_ERROR();

    pState->ext.glShaderSource(pProgram->vertexShader, cVertexSources, apszVertexSources, NULL);
    GL_CHECK_ERROR();

    pState->ext.glCompileShader(pProgram->vertexShader);
    GL_CHECK_ERROR();

    pState->ext.glGetShaderiv(pProgram->vertexShader, GL_COMPILE_STATUS, &success);
    GL_CHECK_ERROR();
    if (!success)
    {
        pState->ext.glGetShaderInfoLog(pProgram->vertexShader, sizeof(szInfoLog), NULL, szInfoLog);
        GL_CHECK_ERROR();
        LogRelMax(10, ("VMSVGA: Vertex shader compilation error:\n%s\n", szInfoLog));
    };

    /*
     * FRAGMENT shader.
     */
    pProgram->fragmentShader = pState->ext.glCreateShader(GL_FRAGMENT_SHADER);
    GL_CHECK_ERROR();

    pState->ext.glShaderSource(pProgram->fragmentShader, cFragmentSources, apszFragmentSources, NULL);
    GL_CHECK_ERROR();

    pState->ext.glCompileShader(pProgram->fragmentShader);
    GL_CHECK_ERROR();

    pState->ext.glGetShaderiv(pProgram->fragmentShader, GL_COMPILE_STATUS, &success);
    GL_CHECK_ERROR();
    if (!success)
    {
        pState->ext.glGetShaderInfoLog(pProgram->fragmentShader, sizeof(szInfoLog), NULL, szInfoLog);
        GL_CHECK_ERROR();
        LogRelMax(10, ("VMSVGA: Fragment shader compilation error:\n%s\n", szInfoLog));
    };

    /*
     * Program
     */
    pProgram->program = pState->ext.glCreateProgram();
    GL_CHECK_ERROR();

    pState->ext.glAttachShader(pProgram->program, pProgram->vertexShader);
    GL_CHECK_ERROR();

    pState->ext.glAttachShader(pProgram->program, pProgram->fragmentShader);
    GL_CHECK_ERROR();

    pState->ext.glLinkProgram(pProgram->program);
    GL_CHECK_ERROR();

    pState->ext.glGetProgramiv(pProgram->program, GL_LINK_STATUS, &success);
    if(!success)
    {
        pState->ext.glGetProgramInfoLog(pProgram->program, sizeof(szInfoLog), NULL, szInfoLog);
        GL_CHECK_ERROR();
        LogRelMax(10, ("VMSVGA: Shader program link error:\n%s\n", szInfoLog));
    }

    pProgram->sSourceTex = pState->ext.glGetUniformLocation(pProgram->program, "sSourceTex");
    GL_CHECK_ERROR();

    pProgram->uTexInfo = pState->ext.glGetUniformLocation(pProgram->program, "uTexInfo");
    GL_CHECK_ERROR();
}

/* Delete a shader program and associated shaders. */
static void deleteShaderProgram(PVMSVGA3DSTATE pState,
                                ShaderProgram *pProgram)
{
    AssertReturnVoid(pState->idActiveContext == VMSVGA3D_SHARED_CTX_ID);

    /* Everything is done on the shared context. The pState and pContext are for GL_CHECK_ERROR macro. */
    PVMSVGA3DCONTEXT pContext = &pState->SharedCtx;

    if (pProgram->program)
    {
        if (pProgram->vertexShader)
        {
            pState->ext.glDetachShader(pProgram->program, pProgram->vertexShader);
            GL_CHECK_ERROR();

            pState->ext.glDeleteShader(pProgram->vertexShader);
            GL_CHECK_ERROR();
        }

        if (pProgram->fragmentShader)
        {
            pState->ext.glDetachShader(pProgram->program, pProgram->fragmentShader);
            GL_CHECK_ERROR();

            pState->ext.glDeleteShader(pProgram->fragmentShader);
            GL_CHECK_ERROR();
        }

        pState->ext.glDeleteProgram(pProgram->program);
        GL_CHECK_ERROR();
    }

    RT_ZERO(*pProgram);
}

/* Initialize the format conversion. Allocate and create necessary resources. */
static void formatConversionInit(PVMSVGA3DSTATE pState)
{
    AssertReturnVoid(pState->idActiveContext == VMSVGA3D_SHARED_CTX_ID);

    PVMSVGA3DFORMATCONVERTER pConv = pState->pConv;
    AssertReturnVoid(pConv);

    /* The pState and pContext variables are for GL_CHECK_ERROR macro. */
    PVMSVGA3DCONTEXT pContext = &pState->SharedCtx;

    pConv->pState = pState;

    /*
     * Shader programs.
     */
    static const GLchar *apszVertexShaderSources[] =
    {
        shaderHeaderSource,
        vertexShaderSource
    };

    static const GLchar * apszYUY2ToRGBSources[] =
    {
        shaderHeaderSource,
        fetchYUY2Source,
        YUV2RGBShaderSource
    };

    static const GLchar *apszUYVYToRGBSources[] =
    {
        shaderHeaderSource,
        fetchUYVYSource,
        YUV2RGBShaderSource
    };

    static const GLchar *apszYUY2FromRGBSources[] =
    {
        shaderHeaderSource,
        storeYUY2Source,
        RGB2YUVShaderSource
    };

    static const GLchar *apszUYVYFromRGBSources[] =
    {
        shaderHeaderSource,
        storeUYVYSource,
        RGB2YUVShaderSource
    };

    createShaderProgram(pState, &pConv->programYUY2ToRGB,
                        RT_ELEMENTS(apszVertexShaderSources), apszVertexShaderSources,
                        RT_ELEMENTS(apszYUY2ToRGBSources), apszYUY2ToRGBSources);

    createShaderProgram(pState, &pConv->programUYVYToRGB,
                        RT_ELEMENTS(apszVertexShaderSources), apszVertexShaderSources,
                        RT_ELEMENTS(apszUYVYToRGBSources), apszUYVYToRGBSources);

    createShaderProgram(pState, &pConv->programYUY2FromRGB,
                        RT_ELEMENTS(apszVertexShaderSources), apszVertexShaderSources,
                        RT_ELEMENTS(apszYUY2FromRGBSources), apszYUY2FromRGBSources);

    createShaderProgram(pState, &pConv->programUYVYFromRGB,
                        RT_ELEMENTS(apszVertexShaderSources), apszVertexShaderSources,
                        RT_ELEMENTS(apszUYVYFromRGBSources), apszUYVYFromRGBSources);

    /*
     * Create a framebuffer object which is used for rendering to a texture.
     */
    pState->ext.glGenFramebuffers(1, &pConv->framebuffer);
    GL_CHECK_ERROR();

    pState->ext.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, pConv->framebuffer);
    GL_CHECK_ERROR();

    static GLenum aDrawBuffers[] = { GL_COLOR_ATTACHMENT0 };
    pState->ext.glDrawBuffers(RT_ELEMENTS(aDrawBuffers), aDrawBuffers);
    GL_CHECK_ERROR();

    pState->ext.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    GL_CHECK_ERROR();

    /*
     * Vertex attribute array.
     */
    pState->ext.glGenBuffers(1, &pConv->vertexBuffer);
    GL_CHECK_ERROR();

    pState->ext.glBindBuffer(GL_ARRAY_BUFFER, pConv->vertexBuffer);
    GL_CHECK_ERROR();

    pState->ext.glBufferData(GL_ARRAY_BUFFER, sizeof(aAttribData), aAttribData, GL_STATIC_DRAW);
    GL_CHECK_ERROR();

    pState->ext.glBindBuffer(GL_ARRAY_BUFFER, 0);
    GL_CHECK_ERROR();
}

/* Delete everything. */
static void formatConversionDestroy(PVMSVGA3DSTATE pState)
{
    AssertReturnVoid(pState->idActiveContext == VMSVGA3D_SHARED_CTX_ID);

    PVMSVGA3DFORMATCONVERTER pConv = pState->pConv;
    AssertReturnVoid(pConv);

    /* The pState and pContext variables are for GL_CHECK_ERROR macro. */
    PVMSVGA3DCONTEXT pContext = &pState->SharedCtx;

    if (pConv->framebuffer != 0)
    {
        /* The code keeps nothing attached. */
        pState->ext.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, pConv->framebuffer);
        GL_CHECK_ERROR();

        GLint texture = -1;
        pState->ext.glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &texture);
        GL_CHECK_ERROR();
        AssertMsg(texture == 0, ("texture %d\n", texture));

        pState->ext.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        GL_CHECK_ERROR();

        pState->ext.glDeleteFramebuffers(1, &pConv->framebuffer);
        GL_CHECK_ERROR();

        pConv->framebuffer = 0;
    }

    deleteShaderProgram(pState, &pConv->programUYVYFromRGB);
    deleteShaderProgram(pState, &pConv->programYUY2FromRGB);
    deleteShaderProgram(pState, &pConv->programUYVYToRGB);
    deleteShaderProgram(pState, &pConv->programYUY2ToRGB);

    if (pConv->vertexBuffer)
    {
        pState->ext.glDeleteBuffers(1, &pConv->vertexBuffer);
        GL_CHECK_ERROR();

        pConv->vertexBuffer = 0;
    }

    pConv->pState = 0;
}

/* Make use of a shader program for the current context and initialize the program uniforms. */
static void setShaderProgram(PVMSVGA3DSTATE pState,
                             ShaderProgram *pProgram,
                             uint32_t cWidth,
                             uint32_t cHeight)
{
    AssertReturnVoid(pState->idActiveContext == VMSVGA3D_SHARED_CTX_ID);

    /* Everything is done on the shared context. The pState and pContext are for GL_CHECK_ERROR macro. */
    PVMSVGA3DCONTEXT pContext = &pState->SharedCtx;

    pState->ext.glUseProgram(pProgram->program);
    GL_CHECK_ERROR();

    pState->ext.glUniform1i(pProgram->sSourceTex, 0);
    GL_CHECK_ERROR();

    float aTextureInfo[4];
    aTextureInfo[0] = (float)cWidth;
    aTextureInfo[1] = (float)cHeight;
    aTextureInfo[2] = 1.0f / (float)cWidth;  /* Pixel width in texture coords. */
    aTextureInfo[3] = 1.0f / (float)cHeight; /* Pixel height in texture coords. */

    pState->ext.glUniform4fv(pProgram->uTexInfo, 1, aTextureInfo);
    GL_CHECK_ERROR();
}

/* Attach the texture which must be used as the render target
 * to the GL_DRAW_FRAMEBUFFER as GL_COLOR_ATTACHMENT0.
 */
static void setRenderTarget(PVMSVGA3DSTATE pState,
                            GLuint texture,
                            uint32_t iMipmap)
{
    AssertReturnVoid(pState->idActiveContext == VMSVGA3D_SHARED_CTX_ID);

    PVMSVGA3DFORMATCONVERTER pConv = pState->pConv;
    AssertReturnVoid(pConv);

    /* Everything is done on the shared context. The pState and pContext are for GL_CHECK_ERROR macro. */
    PVMSVGA3DCONTEXT pContext = &pState->SharedCtx;

    pState->ext.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, pConv->framebuffer);
    GL_CHECK_ERROR();

    glBindTexture(GL_TEXTURE_2D, texture);
    GL_CHECK_ERROR();

    pState->ext.glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, iMipmap);
    GL_CHECK_ERROR();

    glBindTexture(GL_TEXTURE_2D, 0);
    GL_CHECK_ERROR();

    Assert(pState->ext.glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
}

/* Undo what setRenderTarget did. */
static void unsetRenderTarget(PVMSVGA3DSTATE pState,
                              GLuint texture)
{
    AssertReturnVoid(pState->idActiveContext == VMSVGA3D_SHARED_CTX_ID);

    /* Everything is done on the shared context. The pState and pContext are for GL_CHECK_ERROR macro. */
    PVMSVGA3DCONTEXT pContext = &pState->SharedCtx;

    RT_NOREF(texture);

    pState->ext.glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    GL_CHECK_ERROR();

    pState->ext.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    GL_CHECK_ERROR();
}

/** Convert one texture to another.
 *
 * @param pState          The backend.
 * @param pCurrentContext The current context, which must be restored before returning.
 * @param pSurface        The surface which needs conversion.
 * @param iMipmap         The mipmap level which needs to be converted.
 * @param fToRGB          True for conversion from the intermediate texture emulated format
 *                        to the RGB format of the actual texture.
 *                        False for conversion from the actual RGB texture to the intermediate texture.
 */
static void doRender(PVMSVGA3DSTATE pState,
                     PVMSVGA3DCONTEXT pCurrentContext,
                     PVMSVGA3DSURFACE pSurface,
                     uint32_t iMipmap,
                     bool fToRGB)
{
    if (!fToRGB)
    {
        /** @todo Disable readback transfers for now. They cause crash in glDrawArrays with Mesa 19.2 after
         * a previously converted texture is deleted and another texture is being converted.
         * Such transfer are useless anyway for the emulated YUV formats and the guest should not need them usually.
         */
        return;
    }

    LogFunc(("formatConversion: idActiveContext %u, pConv %p, sid=%u, oglid=%u, oglidEmul=%u, mm=%u, %s\n",
             pState->idActiveContext, pState->pConv, pSurface->id, pSurface->oglId.texture, pSurface->idEmulated, iMipmap, fToRGB ? "ToRGB" : "FromRGB"));

    PVMSVGA3DFORMATCONVERTER pConv = pState->pConv;
    AssertReturnVoid(pConv);

    ShaderProgram *pProgram = NULL;
    GLuint sourceTexture = 0;
    GLuint targetTexture = 0;
    if (fToRGB)
    {
        if (pSurface->format == SVGA3D_YUY2)
        {
            pProgram = &pConv->programYUY2ToRGB;
        }
        else if (pSurface->format == SVGA3D_UYVY)
        {
            pProgram = &pConv->programUYVYToRGB;
        }
        sourceTexture = pSurface->idEmulated;
        targetTexture = pSurface->oglId.texture;
    }
    else
    {
        if (pSurface->format == SVGA3D_YUY2)
        {
            pProgram = &pConv->programYUY2FromRGB;
        }
        else if (pSurface->format == SVGA3D_UYVY)
        {
            pProgram = &pConv->programUYVYFromRGB;
        }
        sourceTexture = pSurface->oglId.texture;
        targetTexture = pSurface->idEmulated;
    }

    AssertReturnVoid(pProgram);

    PVMSVGA3DMIPMAPLEVEL pMipmapLevel;
    int rc = vmsvga3dMipmapLevel(pSurface, 0, iMipmap, &pMipmapLevel);
    AssertRCReturnVoid(rc);

    uint32_t const cWidth = pMipmapLevel->mipmapSize.width;
    uint32_t const cHeight = pMipmapLevel->mipmapSize.height;

    /* Use the shared context, where all textures are created. */
    PVMSVGA3DCONTEXT pContext = &pState->SharedCtx;
    VMSVGA3D_SET_CURRENT_CONTEXT(pState, pContext);

    setShaderProgram(pState, pProgram, cWidth, cHeight);

    setRenderTarget(pState, targetTexture, iMipmap);

    glViewport(0, 0, cWidth, cHeight);
    GL_CHECK_ERROR();

    glDisable(GL_DEPTH_TEST);
    GL_CHECK_ERROR();

    pState->ext.glActiveTexture(GL_TEXTURE0);
    GL_CHECK_ERROR();

    glBindTexture(GL_TEXTURE_2D, sourceTexture);
    GL_CHECK_ERROR();

    /* Make sure to set the simplest filter. Otherwise the conversion will not work. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    GL_CHECK_ERROR();

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GL_CHECK_ERROR();

    pState->ext.glBindBuffer(GL_ARRAY_BUFFER, pConv->vertexBuffer);
    GL_CHECK_ERROR();

    GLuint index;
    for (index = 0; index < RT_ELEMENTS(aVertexAttribs); ++index)
    {
        pState->ext.glEnableVertexAttribArray(index);
        GL_CHECK_ERROR();

        pState->ext.glVertexAttribPointer(index, aVertexAttribs[index].size, aVertexAttribs[index].type,
                                          aVertexAttribs[index].normalized, aVertexAttribs[index].stride,
                                          (const GLvoid *)(uintptr_t)aVertexAttribs[index].offset);
        GL_CHECK_ERROR();
    }

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    GL_CHECK_ERROR();

    pState->ext.glBindBuffer(GL_ARRAY_BUFFER, 0);
    GL_CHECK_ERROR();

    glBindTexture(GL_TEXTURE_2D, 0);
    GL_CHECK_ERROR();

    unsetRenderTarget(pState, targetTexture);

    pState->ext.glUseProgram(0);
    GL_CHECK_ERROR();

    for (index = 0; index < RT_ELEMENTS(aVertexAttribs); ++index)
    {
        pState->ext.glDisableVertexAttribArray(index);
        GL_CHECK_ERROR();
    }

    /* Restore the current context. */
    VMSVGA3D_SET_CURRENT_CONTEXT(pState, pCurrentContext);
}

void FormatConvUpdateTexture(PVMSVGA3DSTATE pState,
                             PVMSVGA3DCONTEXT pCurrentContext,
                             PVMSVGA3DSURFACE pSurface,
                             uint32_t iMipmap)
{
    doRender(pState, pCurrentContext, pSurface, iMipmap, true);
}

void FormatConvReadTexture(PVMSVGA3DSTATE pState,
                           PVMSVGA3DCONTEXT pCurrentContext,
                           PVMSVGA3DSURFACE pSurface,
                           uint32_t iMipmap)
{
    doRender(pState, pCurrentContext, pSurface, iMipmap, false);
}

void vmsvga3dOnSharedContextDefine(PVMSVGA3DSTATE pState)
{
    /* Use the shared context, where all textures are created. */
    PVMSVGA3DCONTEXT pContext = &pState->SharedCtx;
    VMSVGA3D_SET_CURRENT_CONTEXT(pState, pContext);

    /*
     * Format conversion.
     */
    Assert(pState->pConv == NULL);

    pState->pConv = (VMSVGA3DFORMATCONVERTER *)RTMemAllocZ(sizeof(VMSVGA3DFORMATCONVERTER));
    AssertReturnVoid(pState->pConv);

    formatConversionInit(pState);
}

void vmsvga3dOnSharedContextDestroy(PVMSVGA3DSTATE pState)
{
    /* Use the shared context, where all textures are created. */
    PVMSVGA3DCONTEXT pContext = &pState->SharedCtx;
    VMSVGA3D_SET_CURRENT_CONTEXT(pState, pContext);

    if (pState->pConv)
    {
        formatConversionDestroy(pState);

        RTMemFree(pState->pConv);
        pState->pConv = NULL;
    }
}
