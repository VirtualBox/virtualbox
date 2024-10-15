/* $Id: oglrender.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * OpenGL testcase. Interface for OpenGL tests.
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_ogltest_oglrender_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_ogltest_oglrender_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifdef VBOX
#include <iprt/win/windows.h>
#else
#include <windows.h>
#endif
#include <GL/gl.h>
#include <GL/glu.h>
#include <glext.h>

inline void TestShowError(HRESULT hr, const char *pszString)
{
    (void)hr;
    MessageBoxA(0, pszString, 0, 0);
}
/* Expand __LINE__ number to a string. */
#define OGLTEST_S(n) #n
#define OGLTEST_N2S(n) OGLTEST_S(n)

#define GL_CHECK_ERROR() do { if (glGetError() != GL_NO_ERROR) TestShowError((E_FAIL), __FILE__ "@" OGLTEST_N2S(__LINE__)); } while(0)

class OGLRender
{
public:
    OGLRender() {}
    virtual ~OGLRender() {}
    virtual HRESULT InitRender() = 0;
    virtual HRESULT DoRender() = 0;
    virtual void TimeAdvance(float dt) { (void)dt; return; }
};

OGLRender *CreateRender(int iRenderId);
void DeleteRender(OGLRender *pRender);

extern PFNGLBINDBUFFERPROC                             glBindBuffer;
extern PFNGLDELETEBUFFERSPROC                          glDeleteBuffers;
extern PFNGLGENBUFFERSPROC                             glGenBuffers;
extern PFNGLBUFFERDATAPROC                             glBufferData;
extern PFNGLMAPBUFFERPROC                              glMapBuffer;
extern PFNGLUNMAPBUFFERPROC                            glUnmapBuffer;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC                glEnableVertexAttribArray;
extern PFNGLDISABLEVERTEXATTRIBARRAYPROC               glDisableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC                    glVertexAttribPointer;
extern PFNGLCREATESHADERPROC                           glCreateShader;
extern PFNGLATTACHSHADERPROC                           glAttachShader;
extern PFNGLCOMPILESHADERPROC                          glCompileShader;
extern PFNGLCREATEPROGRAMPROC                          glCreateProgram;
extern PFNGLDELETEPROGRAMPROC                          glDeleteProgram;
extern PFNGLDELETESHADERPROC                           glDeleteShader;
extern PFNGLDETACHSHADERPROC                           glDetachShader;
extern PFNGLLINKPROGRAMPROC                            glLinkProgram;
extern PFNGLSHADERSOURCEPROC                           glShaderSource;
extern PFNGLUSEPROGRAMPROC                             glUseProgram;
extern PFNGLGETPROGRAMIVPROC                           glGetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC                      glGetProgramInfoLog;
extern PFNGLGETSHADERIVPROC                            glGetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC                       glGetShaderInfoLog;
extern PFNGLVERTEXATTRIBDIVISORPROC                    glVertexAttribDivisor;
extern PFNGLDRAWARRAYSINSTANCEDPROC                    glDrawArraysInstanced;

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_ogltest_oglrender_h */
