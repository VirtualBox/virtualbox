/* $Id: DevVGA-SVGA3d-glLdr.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevVGA - VMWare SVGA device - 3D part, dynamic loading of GL function.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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

#define VMSVGA3D_GL_DEFINE_PFN
#include "DevVGA-SVGA3d-glLdr.h"

#include <VBox/vmm/pdmdev.h>
#include <VBox/err.h>
#include <iprt/assert.h>
#include <iprt/cdefs.h>
#include <iprt/ldr.h>
#include <iprt/log.h>

#ifdef RT_OS_WINDOWS
# define OGLGETPROCADDRESS MyWinGetProcAddress
DECLINLINE(PFNRT) MyWinGetProcAddress(const char *pszSymbol)
{
    int rc;

    static RTLDRMOD s_hOpenGL32 = NULL;
    if (s_hOpenGL32 == NULL)
    {
        rc = RTLdrLoadSystem("opengl32", /* fNoUnload = */ true, &s_hOpenGL32);
        if (RT_FAILURE(rc))
           s_hOpenGL32 = NULL;
    }

    typedef PROC (WINAPI *PFNWGLGETPROCADDRESS)(LPCSTR);
    static PFNWGLGETPROCADDRESS s_wglGetProcAddress = NULL;
    if (s_wglGetProcAddress == NULL)
    {
        if (s_hOpenGL32 != NULL)
        {
            rc = RTLdrGetSymbol(s_hOpenGL32, "wglGetProcAddress", (void **)&s_wglGetProcAddress);
            if (RT_FAILURE(rc))
               s_wglGetProcAddress = NULL;
        }
    }

    if (s_wglGetProcAddress)
    {
        /* Khronos: [on failure] "some implementations will return other values. 1, 2, and 3 are used, as well as -1". */
        PFNRT p = (PFNRT)s_wglGetProcAddress(pszSymbol);
        if (RT_VALID_PTR(p))
            return p;

        /* Might be an exported symbol. */
        rc = RTLdrGetSymbol(s_hOpenGL32, pszSymbol, (void **)&p);
        if (RT_SUCCESS(rc))
            return p;
    }

    return 0;
}

#elif defined(RT_OS_DARWIN)
# include <dlfcn.h>
# define OGLGETPROCADDRESS      MyNSGLGetProcAddress
/** Resolves an OpenGL symbol.  */
static void *MyNSGLGetProcAddress(const char *pszSymbol)
{
    /* Another copy in shaderapi.c. */
    static void *s_pvImage = NULL;
    if (s_pvImage == NULL)
        s_pvImage = dlopen("/System/Library/Frameworks/OpenGL.framework/Versions/Current/OpenGL", RTLD_LAZY);
    return s_pvImage ? dlsym(s_pvImage, pszSymbol) : NULL;
}

#else
# define OGLGETPROCADDRESS MyGLXGetProcAddress
static PFNRT MyGLXGetProcAddress(const char *pszSymbol)
{
    int rc;

    static RTLDRMOD s_hGL = NULL;
    if (s_hGL == NULL)
    {
        static const char s_szLibGL[] = "libGL.so.1";
        rc = RTLdrLoadEx(s_szLibGL, &s_hGL, RTLDRLOAD_FLAGS_GLOBAL | RTLDRLOAD_FLAGS_NO_UNLOAD, NULL);
        if (RT_FAILURE(rc))
        {
            LogRel(("VMSVGA3d: failed to load %s: %Rrc\n", s_szLibGL, rc));
            s_hGL = NULL;
            return NULL;
        }
    }

    typedef PFNRT (* PFNGLXGETPROCADDRESS)(const GLubyte * procName);
    static PFNGLXGETPROCADDRESS s_glXGetProcAddress = NULL;
    if (s_glXGetProcAddress == NULL)
    {
        rc = RTLdrGetSymbol(s_hGL, "glXGetProcAddress", (void **)&s_glXGetProcAddress);
        if (RT_FAILURE(rc))
        {
            LogRel(("VMSVGA3d: failed to get glXGetProcAddress: %Rrc\n", rc));
            s_glXGetProcAddress = NULL;
            return NULL;
        }
    }

    PFNRT p = s_glXGetProcAddress((const GLubyte *)pszSymbol);
    if (RT_VALID_PTR(p))
        return p;

    /* Might be an exported symbol. */
    rc = RTLdrGetSymbol(s_hGL, pszSymbol, (void **)&p);
    if (RT_SUCCESS(rc))
        return p;

    return NULL;
}

static PFNRT MyX11GetProcAddress(const char *pszSymbol)
{
    int rc;

    static RTLDRMOD s_hX11 = NULL;
    if (s_hX11 == NULL)
    {
        static const char s_szLibX11[] = "libX11.so.6";
        rc = RTLdrLoadEx(s_szLibX11, &s_hX11, RTLDRLOAD_FLAGS_LOCAL | RTLDRLOAD_FLAGS_NO_UNLOAD, NULL);
        if (RT_FAILURE(rc))
        {
            LogRel(("VMSVGA3d: failed to load %s: %Rrc\n", s_szLibX11, rc));
            s_hX11 = NULL;
            return NULL;
        }
    }

    PFNRT p = NULL;
    rc = RTLdrGetSymbol(s_hX11, pszSymbol, (void **)&p);
    if (RT_SUCCESS(rc))
        return p;

    return NULL;
}

#define X11GETPROC_(ProcName) do { \
    *(PFNRT *)&pfn_##ProcName = pfnRet = MyX11GetProcAddress(#ProcName); \
    if (pfnRet) { /* likely */ } \
    else \
    { \
        AssertLogRelMsg(pfnRet, ("%s missing\n", #ProcName)); \
        return PDMDevHlpVMSetError(pDevIns, VERR_VGA_GL_SYMBOL_NOT_FOUND, RT_SRC_POS, \
                                   "Missing libX11 symbol '%s'\n", #ProcName); \
    } \
} while(0)
#endif

#define GLGETPROC_(ProcName, NameSuffix) do { \
    *(PFNRT *)&pfn_##ProcName = pfnRet = OGLGETPROCADDRESS(#ProcName NameSuffix); \
    if (pfnRet) { /* likely */ } \
    else \
    { \
        AssertLogRelMsg(pfnRet, ("%s missing\n", #ProcName NameSuffix)); \
        return PDMDevHlpVMSetError(pDevIns, VERR_VGA_GL_SYMBOL_NOT_FOUND, RT_SRC_POS, \
                                   "Missing OpenGL symbol '%s'\n", #ProcName NameSuffix); \
    } \
} while(0)

int glLdrInit(PPDMDEVINS pDevIns)
{
    /** @todo r=bird: Perhaps make this template include file driven? See
     * include/VBox/dbus.h, include/VBox/dbus-calls.h and iprt/runtime-loader.h for
     * instance.  Regardless, it would be would be nice if we could move up the
     * RTLdrLoadSystem/dlopen bits and have separate error reporting for those,
     * making use of VERR_VGA_GL_LOAD_FAILURE.  I can look into that, but
     * probably only after the release is out... */

#ifdef RT_OS_WINDOWS
    pfn_wglCreateContext = 0;
    pfn_wglDeleteContext = 0;
    pfn_wglMakeCurrent = 0;
    pfn_wglShareLists = 0;
#elif defined(RT_OS_LINUX)
    pfn_XConfigureWindow = 0;
    pfn_XCloseDisplay = 0;
    pfn_XCreateColormap = 0;
    pfn_XCreatePixmap = 0;
    pfn_XCreateWindow = 0;
    pfn_XDefaultRootWindow = 0;
    pfn_XDestroyWindow = 0;
    pfn_XFree = 0;
    pfn_XFreePixmap = 0;
    pfn_XInitThreads = 0;
    pfn_XNextEvent = 0;
    pfn_XOpenDisplay = 0;
    pfn_XPending = 0;
    pfn_XSetErrorHandler = 0;
    pfn_XSync = 0;
    pfn_XScreenNumberOfScreen = 0;
    pfn_XMapWindow = 0;
    pfn_XGetWindowAttributes = 0;
    pfn_glXGetFBConfigAttrib = 0;
    pfn_glXGetVisualFromFBConfig = 0;
    pfn_glXQueryVersion = 0;
    pfn_glXChooseFBConfig = 0;
    pfn_glXChooseVisual = 0;
    pfn_glXCreateContext = 0;
    pfn_glXCreatePixmap = 0;
    pfn_glXMakeCurrent = 0;
    pfn_glXDestroyContext = 0;
    pfn_glXDestroyPixmap = 0;
#endif
    pfn_glAlphaFunc = 0;
    pfn_glBegin = 0;
    pfn_glBindTexture = 0;
    pfn_glBlendColor = 0;
    pfn_glBlendEquation = 0;
    pfn_glBlendFunc = 0;
    pfn_glClear = 0;
    pfn_glClearColor = 0;
    pfn_glClearDepth = 0;
    pfn_glClearStencil = 0;
    pfn_glClientActiveTexture = 0;
    pfn_glClipPlane = 0;
    pfn_glColorMask = 0;
    pfn_glColorPointer = 0;
    pfn_glCullFace = 0;
    pfn_glDeleteTextures = 0;
    pfn_glDepthFunc = 0;
    pfn_glDepthMask = 0;
    pfn_glDepthRange = 0;
    pfn_glDisable = 0;
    pfn_glDisableClientState = 0;
    pfn_glDrawArrays = 0;
    pfn_glDrawElements = 0;
    pfn_glEnable = 0;
    pfn_glEnableClientState = 0;
    pfn_glEnd = 0;
    pfn_glFinish = 0;
    pfn_glFlush = 0;
    pfn_glFogf = 0;
    pfn_glFogfv = 0;
    pfn_glFogi = 0;
    pfn_glFrontFace = 0;
    pfn_glGenTextures = 0;
    pfn_glGetBooleanv = 0;
    pfn_glGetError = 0;
    pfn_glGetFloatv = 0;
    pfn_glGetIntegerv = 0;
    pfn_glGetString = 0;
    pfn_glGetTexImage = 0;
    pfn_glLightModelfv = 0;
    pfn_glLightf = 0;
    pfn_glLightfv = 0;
    pfn_glLineWidth = 0;
    pfn_glLoadIdentity = 0;
    pfn_glLoadMatrixf = 0;
    pfn_glMaterialfv = 0;
    pfn_glMatrixMode = 0;
    pfn_glMultMatrixf = 0;
    pfn_glNormalPointer = 0;
    pfn_glOrtho = 0;
    pfn_glPixelStorei = 0;
    pfn_glPointSize = 0;
    pfn_glPolygonMode = 0;
    pfn_glPolygonOffset = 0;
    pfn_glPopAttrib = 0;
    pfn_glPopMatrix = 0;
    pfn_glPushAttrib = 0;
    pfn_glPushMatrix = 0;
    pfn_glScissor = 0;
    pfn_glShadeModel = 0;
    pfn_glStencilFunc = 0;
    pfn_glStencilMask = 0;
    pfn_glStencilOp = 0;
    pfn_glTexCoord2f = 0;
    pfn_glTexCoordPointer = 0;
    pfn_glTexImage2D = 0;
    pfn_glTexParameterf = 0;
    pfn_glTexParameterfv = 0;
    pfn_glTexParameteri = 0;
    pfn_glTexSubImage2D = 0;
    pfn_glVertex2i = 0;
    pfn_glVertexPointer = 0;
    pfn_glViewport = 0;

    PFNRT pfnRet;
#ifdef RT_OS_WINDOWS
    GLGETPROC_(wglCreateContext, "");
    GLGETPROC_(wglDeleteContext, "");
    GLGETPROC_(wglMakeCurrent, "");
    GLGETPROC_(wglShareLists, "");
#elif defined(RT_OS_LINUX)
    X11GETPROC_(XConfigureWindow);
    X11GETPROC_(XCloseDisplay);
    X11GETPROC_(XCreateColormap);
    X11GETPROC_(XCreatePixmap);
    X11GETPROC_(XCreateWindow);
    X11GETPROC_(XDefaultRootWindow);
    X11GETPROC_(XDestroyWindow);
    X11GETPROC_(XFree);
    X11GETPROC_(XFreePixmap);
    X11GETPROC_(XInitThreads);
    X11GETPROC_(XNextEvent);
    X11GETPROC_(XOpenDisplay);
    X11GETPROC_(XPending);
    X11GETPROC_(XSetErrorHandler);
    X11GETPROC_(XSync);
    X11GETPROC_(XScreenNumberOfScreen);
    X11GETPROC_(XMapWindow);
    X11GETPROC_(XGetWindowAttributes);
    GLGETPROC_(glXGetFBConfigAttrib, "");
    GLGETPROC_(glXGetVisualFromFBConfig, "");
    GLGETPROC_(glXQueryVersion, "");
    GLGETPROC_(glXChooseFBConfig, "");
    GLGETPROC_(glXChooseVisual, "");
    GLGETPROC_(glXCreateContext, "");
    GLGETPROC_(glXCreatePixmap, "");
    GLGETPROC_(glXMakeCurrent, "");
    GLGETPROC_(glXDestroyContext, "");
    GLGETPROC_(glXDestroyPixmap, "");
#endif
    GLGETPROC_(glAlphaFunc, "");
    GLGETPROC_(glBegin, "");
    GLGETPROC_(glBindTexture, "");
    GLGETPROC_(glBlendFunc, "");
    GLGETPROC_(glClear, "");
    GLGETPROC_(glClearColor, "");
    GLGETPROC_(glClearDepth, "");
    GLGETPROC_(glClearStencil, "");
    GLGETPROC_(glClipPlane, "");
    GLGETPROC_(glColorMask, "");
    GLGETPROC_(glColorPointer, "");
    GLGETPROC_(glCullFace, "");
    GLGETPROC_(glDeleteTextures, "");
    GLGETPROC_(glDepthFunc, "");
    GLGETPROC_(glDepthMask, "");
    GLGETPROC_(glDepthRange, "");
    GLGETPROC_(glDisable, "");
    GLGETPROC_(glDisableClientState, "");
    GLGETPROC_(glDrawArrays, "");
    GLGETPROC_(glDrawElements, "");
    GLGETPROC_(glEnable, "");
    GLGETPROC_(glEnableClientState, "");
    GLGETPROC_(glEnd, "");
    GLGETPROC_(glFinish, "");
    GLGETPROC_(glFlush, "");
    GLGETPROC_(glFogf, "");
    GLGETPROC_(glFogfv, "");
    GLGETPROC_(glFogi, "");
    GLGETPROC_(glFrontFace, "");
    GLGETPROC_(glGenTextures, "");
    GLGETPROC_(glGetBooleanv, "");
    GLGETPROC_(glGetError, "");
    GLGETPROC_(glGetFloatv, "");
    GLGETPROC_(glGetIntegerv, "");
    GLGETPROC_(glGetString, "");
    GLGETPROC_(glGetTexImage, "");
    GLGETPROC_(glLightModelfv, "");
    GLGETPROC_(glLightf, "");
    GLGETPROC_(glLightfv, "");
    GLGETPROC_(glLineWidth, "");
    GLGETPROC_(glLoadIdentity, "");
    GLGETPROC_(glLoadMatrixf, "");
    GLGETPROC_(glMaterialfv, "");
    GLGETPROC_(glMatrixMode, "");
    GLGETPROC_(glMultMatrixf, "");
    GLGETPROC_(glNormalPointer, "");
    GLGETPROC_(glOrtho, "");
    GLGETPROC_(glPixelStorei, "");
    GLGETPROC_(glPointSize, "");
    GLGETPROC_(glPolygonMode, "");
    GLGETPROC_(glPolygonOffset, "");
    GLGETPROC_(glPopAttrib, "");
    GLGETPROC_(glPopMatrix, "");
    GLGETPROC_(glPushAttrib, "");
    GLGETPROC_(glPushMatrix, "");
    GLGETPROC_(glScissor, "");
    GLGETPROC_(glShadeModel, "");
    GLGETPROC_(glStencilFunc, "");
    GLGETPROC_(glStencilMask, "");
    GLGETPROC_(glStencilOp, "");
    GLGETPROC_(glTexCoord2f, "");
    GLGETPROC_(glTexCoordPointer, "");
    GLGETPROC_(glTexImage2D, "");
    GLGETPROC_(glTexParameterf, "");
    GLGETPROC_(glTexParameterfv, "");
    GLGETPROC_(glTexParameteri, "");
    GLGETPROC_(glTexSubImage2D, "");
    GLGETPROC_(glVertex2i, "");
    GLGETPROC_(glVertexPointer, "");
    GLGETPROC_(glViewport, "");

#ifdef RT_OS_LINUX
    XInitThreads();
#endif
    return VINF_SUCCESS;
}

PFNRT glLdrGetProcAddress(const char *pszSymbol)
{
    return OGLGETPROCADDRESS(pszSymbol);
}

int glLdrGetExtFunctions(PPDMDEVINS pDevIns)
{
    PFNRT pfnRet;
    GLGETPROC_(glBlendColor, "");
    GLGETPROC_(glBlendEquation, "");
    GLGETPROC_(glClientActiveTexture, "");
    return VINF_SUCCESS;
}
