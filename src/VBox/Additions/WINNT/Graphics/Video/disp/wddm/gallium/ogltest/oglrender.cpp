/* $Id: oglrender.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * OpenGL testcase. Simple OpenGL tests.
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

#include "oglrender.h"


/*
 * Old style glBegin/glEnd colored triangle
 */
class OGLRenderTriangle : public OGLRender
{
    virtual HRESULT InitRender();
    virtual HRESULT DoRender();
};

HRESULT OGLRenderTriangle::InitRender()
{
    return S_OK;
}

HRESULT OGLRenderTriangle::DoRender()
{
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
    glColor3f ( 1.0f,  0.0f, 0.0f);
    glVertex2f(-1.0f, -1.0f);
    glColor3f ( 0.0f,  1.0f, 0.0f);
    glVertex2f( 0.0f,  1.0f);
    glColor3f ( 0.0f,  0.0f, 1.0f);
    glVertex2f( 1.0f, -1.0f);
    glEnd();
    glFlush();
    return S_OK;
}


/*
 * Texture2D.
 */
class OGLRenderTexture2D : public OGLRender
{
    virtual HRESULT InitRender();
    virtual HRESULT DoRender();
    GLuint texName;
    static const int texWidth = 8;
    static const int texHeight = 8;
};

HRESULT OGLRenderTexture2D::InitRender()
{
    static GLubyte texImage[texHeight][texWidth][4];
    for (int y = 0; y < texHeight; ++y)
    {
       for (int x = 0; x < texWidth; ++x)
       {
          GLubyte v = 255;
          if (   (texHeight/4 <= y && y < 3*texHeight/4)
              && (texWidth/4 <= x && x < 3*texWidth/4))
          {
              if (y < x)
              {
                  v = 0;
              }
          }

          texImage[y][x][0] = v;
          texImage[y][x][1] = 0;
          texImage[y][x][2] = 0;
          texImage[y][x][3] = 255;
       }
    }

    glClearColor(0.0, 0.0, 1.0, 1.0);

    glGenTextures(1, &texName);
    glBindTexture(GL_TEXTURE_2D, texName);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texWidth, texHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, texImage);

    glBindTexture(GL_TEXTURE_2D, 0);

    return S_OK;
}

HRESULT OGLRenderTexture2D::DoRender()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_TEXTURE_2D);

    glBindTexture(GL_TEXTURE_2D, texName);

    glBegin(GL_TRIANGLES);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 0.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, 0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, 0.0f);

    glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f( 0.0f, -1.0f, 0.0f);
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);

    glDisable(GL_TEXTURE_2D);

    glFlush();

    return S_OK;
}


/*
 * DrawArraysInstanced. Uses shaders from a guest.
 */
class OGLRenderDrawArrays : public OGLRender
{
    static const int cArrays = 4;

    struct VertexAttribDesc
    {
        GLint     size;
        GLenum    type;
        GLboolean normalized;
        GLsizei   stride;
    };

    static VertexAttribDesc aVertexAttribs[cArrays];
    GLuint vbNames[cArrays];

    GLuint vertexShader;
    GLuint fragmentShader;
    GLuint program;

    virtual HRESULT InitRender();
    virtual HRESULT DoRender();
};

static const GLchar *apszVertexShader[] =
{
    "  #version 120\n"
    "  #extension GL_EXT_gpu_shader4 : enable\n"
    "  uniform vec4 VC[2048];\n"
    "  uniform vec4 posFixup;\n"
    "  void order_ps_input(in vec4[12]);\n"
    "  vec4 OUT[12];\n"
    "  vec4 R0;\n"
    "  vec4 R1;\n"
    "  vec4 R2;\n"
    "  attribute vec4 attrib0;\n"
    "  attribute vec4 attrib1;\n"
    "  attribute vec4 attrib2;\n"
    "  attribute vec4 attrib3;\n"
    "  vec4 tmp0;\n"
    "  vec4 tmp1;\n"
    "  bool p0[4];\n"
    "  uniform vec4 VC1 = { 0.000000, 0.000000, 1.00000, 1.000000 };\n"
    "  uniform vec4 VLC2 = { 1.000000, -1.000000, 0.500000, 0.000000 };\n"
    "  const float FLT_MAX = 1e38;\n"
    "  void main() {"
    "      R0.xy = (attrib0.xy);\n"
    "      R0.yzw = (R0.yyy * attrib2.xyz);\n"
    "      R0.xyz = ((attrib1.xyz * R0.xxx) + R0.yzw);\n"
    "      R0.xyz = (R0.xyz + attrib3.xyz);\n"
    "      R1.xyzw = (R0.xzyz * VC1.zxwy); // (R0.xzyz * VC[1].zxwy);\n"
    "      R1.xy = (R1.yw + R1.xz);\n"
    "      R2.xy = (R1.xy * VLC2.xy);\n"
    "      R2.zw = (R0.zz * VLC2.zx);\n"
    "      OUT[1].xyw = (R0.xyz);\n"
    "      OUT[1].z = (VLC2.w);\n"
    "      OUT[0].xyzw = (R2.xyzw);\n"
    "      gl_Position.xyzw = OUT[0].xyzw;\n"
    "      gl_FogFragCoord = 0.0;\n"
    "      //gl_Position.y = gl_Position.y * posFixup.y;\n"
    "      //gl_Position.xy += posFixup.zw * gl_Position.ww;\n"
    "      //gl_Position.z = gl_Position.z * 2.0 - gl_Position.w;\n"
    "  }\n"
};

static const char *passthrough_vshader[] =
{
    "  #version 120\n"
    "  vec4 R0;\n"
    "  attribute vec4 attrib0;\n"
    "  attribute vec4 attrib1;\n"
    "  attribute vec4 attrib2;\n"
    "  attribute vec4 attrib3;\n"
    "  void main(void)\n"
    "  {\n"
    "      R0   = attrib0;\n"
    "      R0.w = 1.0;\n"
    "      R0.z = 0.0;\n"
    "      gl_Position   = R0;\n"
    "  }\n"
};

static const GLchar *apszFragmentShader[] =
{
    "  #version 120\n"
    "  #extension GL_EXT_gpu_shader4 : enable\n"
    "  uniform vec4 PC[2048];\n"
    "  varying vec4 IN[31];\n"
    "  vec4 tmp0;\n"
    "  vec4 tmp1;\n"
    "  bool p0[4];\n"
    "  uniform vec4 PLC0;\n"
    "  const float FLT_MAX = 1e38;\n"
    "  void main() {"
    "      gl_FragData[0].xyzw = vec4(1.0, 1.0, 1.0, 1.0); //(PLC0.xyzw);\n"
    "  }\n"
};

/* static */ OGLRenderDrawArrays::VertexAttribDesc OGLRenderDrawArrays::aVertexAttribs[OGLRenderDrawArrays::cArrays] =
{
    {2, GL_FLOAT, GL_FALSE, 8 },
    {4, GL_FLOAT, GL_FALSE, 0 },
    {4, GL_FLOAT, GL_FALSE, 0 },
    {4, GL_FLOAT, GL_FALSE, 0 }
};

/* Triangle fan. */
static float aAttrib0[] =
{
    0.0f,  200.0f,
  300.0f,  200.0f,
  300.0f,  400.0f,
    0.0f,  400.0f
};

static float aAttrib0a[] =
{
  -1.0f,  -1.0f,
   1.0f,  -1.0f,
   0.0f,   0.0f,
   0.0f,   2.0f
};

static float aAttrib1[] =
{
    // 1 / (w / 2)
    0.001556f, 0.000000f, 0.000000f, 1.000000f,
};

static float aAttrib2[] =
{
               // 1/ (h / 2)
    0.000000f, -0.001874f, 0.000000f, 1.000000f,
};

static float aAttrib3[] =
{
    -1.000000f,  1.000000f, 1.000000f, 1.000000f,
};

HRESULT OGLRenderDrawArrays::InitRender()
{
    glClearColor(0.0, 0.0, 1.0, 1.0);

    int success;
    char szInfoLog[1024];

    vertexShader = glCreateShader(GL_VERTEX_SHADER);
    GL_CHECK_ERROR();
    glShaderSource(vertexShader, 1, apszVertexShader, NULL);
    GL_CHECK_ERROR();
    glCompileShader(vertexShader);
    GL_CHECK_ERROR();
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    GL_CHECK_ERROR();
    if (!success)
    {
        glGetShaderInfoLog(vertexShader, sizeof(szInfoLog), NULL, szInfoLog);
        GL_CHECK_ERROR();
        TestShowError(E_FAIL, szInfoLog);
    };

    fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    GL_CHECK_ERROR();
    glShaderSource(fragmentShader, 1, apszFragmentShader, NULL);
    GL_CHECK_ERROR();
    glCompileShader(fragmentShader);
    GL_CHECK_ERROR();
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    GL_CHECK_ERROR();
    if (!success)
    {
        glGetShaderInfoLog(fragmentShader, sizeof(szInfoLog), NULL, szInfoLog);
        GL_CHECK_ERROR();
        TestShowError(E_FAIL, szInfoLog);
    };

    program = glCreateProgram();
    GL_CHECK_ERROR();
    glAttachShader(program, vertexShader);
    GL_CHECK_ERROR();
    glAttachShader(program, fragmentShader);
    GL_CHECK_ERROR();
    glLinkProgram(program);
    GL_CHECK_ERROR();
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if(!success)
    {
        glGetProgramInfoLog(program, sizeof(szInfoLog), NULL, szInfoLog);
        GL_CHECK_ERROR();
        TestShowError(E_FAIL, szInfoLog);
    }

    glUseProgram(program);
    GL_CHECK_ERROR();

    glGenBuffers(cArrays, vbNames);
    GL_CHECK_ERROR();

    struct AttribData
    {
        GLsizeiptr size;
        const GLvoid * data;
    };

    static struct AttribData attribData[cArrays] =
    {
        { sizeof(aAttrib0), aAttrib0 },
        { sizeof(aAttrib1), aAttrib1 },
        { sizeof(aAttrib2), aAttrib2 },
        { sizeof(aAttrib3), aAttrib3 },
    };

    GLuint index;
    for (index = 0; index < cArrays; ++index)
    {
        glBindBuffer(GL_ARRAY_BUFFER, vbNames[index]);
        GL_CHECK_ERROR();

        glBufferData(GL_ARRAY_BUFFER, attribData[index].size, attribData[index].data, GL_DYNAMIC_DRAW);
        GL_CHECK_ERROR();

        glEnableVertexAttribArray(index);
        GL_CHECK_ERROR();
        glVertexAttribPointer(index, aVertexAttribs[index].size, aVertexAttribs[index].type,
                              aVertexAttribs[index].normalized, aVertexAttribs[index].stride,
                              (const GLvoid *)(uintptr_t)0);
        GL_CHECK_ERROR();

        GLuint const divisor = aVertexAttribs[index].stride ?
                               0 : /* Once per vertex. */
                               1;  /* Once for one (1) set of vertices (or instance). */
        glVertexAttribDivisor(index, divisor);
        GL_CHECK_ERROR();
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    GL_CHECK_ERROR();

    return S_OK;
}

HRESULT OGLRenderDrawArrays::DoRender()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, 1);
    GL_CHECK_ERROR();

    glFlush();
    return S_OK;
}


OGLRender *CreateRender(int iRenderId)
{
    OGLRender *pRender = NULL;
    switch (iRenderId)
    {
        case 0: pRender = new OGLRenderTriangle(); break;
        case 1: pRender = new OGLRenderTexture2D(); break;
        case 2: pRender = new OGLRenderDrawArrays(); break;
        default:
            break;
    }
    return pRender;
}

void DeleteRender(OGLRender *pRender)
{
    if (pRender)
    {
        delete pRender;
    }
}
