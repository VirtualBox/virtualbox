/* $Id: DevVGA-SVGA3d-glLdr.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef VBOX_INCLUDED_SRC_Graphics_DevVGA_SVGA3d_glLdr_h
#define VBOX_INCLUDED_SRC_Graphics_DevVGA_SVGA3d_glLdr_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifndef VMSVGA3D_OPENGL
# error "This include file is for VMSVGA3D_OPENGL."
#endif

#include <VBox/types.h>

/** @todo VBOX_VMSVGA3D_GL_HACK_LEVEL is not necessary when dynamic loading is used. */

#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
# include <GL/gl.h>
# include "vmsvga_glext/wglext.h"

#elif defined(RT_OS_DARWIN)
# include <OpenGL/OpenGL.h>
# include <OpenGL/gl3.h>
# include <OpenGL/gl3ext.h>
# define GL_DO_NOT_WARN_IF_MULTI_GL_VERSION_HEADERS_INCLUDED
# include <OpenGL/gl.h>
# include "DevVGA-SVGA3d-cocoa.h"
// HACK
typedef void (APIENTRYP PFNGLFOGCOORDPOINTERPROC) (GLenum type, GLsizei stride, const GLvoid *pointer);
typedef void (APIENTRYP PFNGLCLIENTACTIVETEXTUREPROC) (GLenum texture);
typedef void (APIENTRYP PFNGLGETPROGRAMIVARBPROC) (GLenum target, GLenum pname, GLint *params);
# define GL_RGBA_S3TC 0x83A2
# define GL_ALPHA8_EXT 0x803c
# define GL_LUMINANCE8_EXT 0x8040
# define GL_LUMINANCE16_EXT 0x8042
# define GL_LUMINANCE4_ALPHA4_EXT 0x8043
# define GL_LUMINANCE8_ALPHA8_EXT 0x8045
# define GL_INT_2_10_10_10_REV 0x8D9F

#else
# include <X11/Xlib.h>
# include <X11/Xatom.h>
# include <GL/gl.h>
# include <GL/glx.h>
# define VBOX_VMSVGA3D_GL_HACK_LEVEL 0x103
#endif

#ifndef __glext_h__
# undef GL_GLEXT_VERSION    /** @todo r=bird: We include GL/glext.h above which also defines this and we'll end up with
                             * a clash if the system one does not use the same header guard as ours.  So, I'm wondering
                             * whether this include is really needed, and if it is, whether we should use a unique header
                             * guard macro on it, so we'll have the same problems everywhere... */
#endif
#include "vmsvga_glext/glext.h"


#ifdef RT_OS_WINDOWS
# define GLAPIENTRY APIENTRY
#else
# define GLAPIENTRY
#endif

#define GLAPIENTRYP GLAPIENTRY *

#ifdef VMSVGA3D_GL_DEFINE_PFN
# define GLPFN
#else
# define GLPFN extern
#endif

/** Load OpenGL library and initialize function pointers. */
int glLdrInit(PPDMDEVINS pDevIns);
/** Resolve an OpenGL function name. */
PFNRT glLdrGetProcAddress(const char *pszSymbol);
/** Get pointers to extension function. They are available on Windows only when OpenGL context is set. */
int glLdrGetExtFunctions(PPDMDEVINS pDevIns);

/*
 * All OpenGL function used by VMSVGA backend.
 */

/*
 * GL 1.1 functions (exported from OpenGL32 on Windows).
 */
GLPFN void (GLAPIENTRYP pfn_glAlphaFunc)(GLenum func, GLclampf ref);
#define glAlphaFunc pfn_glAlphaFunc

GLPFN void (GLAPIENTRYP pfn_glBegin)(GLenum mode);
#define glBegin pfn_glBegin

GLPFN void (GLAPIENTRYP pfn_glBindTexture)(GLenum target, GLuint texture);
#define glBindTexture pfn_glBindTexture

GLPFN void (GLAPIENTRYP pfn_glBlendFunc)(GLenum sfactor, GLenum dfactor);
#define glBlendFunc pfn_glBlendFunc

GLPFN void (GLAPIENTRYP pfn_glClear)(GLbitfield mask);
#define glClear pfn_glClear

GLPFN void (GLAPIENTRYP pfn_glClearColor)(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
#define glClearColor pfn_glClearColor

GLPFN void (GLAPIENTRYP pfn_glClearDepth)(GLclampd depth);
#define glClearDepth pfn_glClearDepth

GLPFN void (GLAPIENTRYP pfn_glClearStencil)(GLint s);
#define glClearStencil pfn_glClearStencil

GLPFN void (GLAPIENTRYP pfn_glClipPlane)(GLenum plane, const GLdouble *equation);
#define glClipPlane pfn_glClipPlane

GLPFN void (GLAPIENTRYP pfn_glColorMask)(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
#define glColorMask pfn_glColorMask

GLPFN void (GLAPIENTRYP pfn_glColorPointer)(GLint size, GLenum type, GLsizei stride, const void *pointer);
#define glColorPointer pfn_glColorPointer

GLPFN void (GLAPIENTRYP pfn_glCullFace)(GLenum mode);
#define glCullFace pfn_glCullFace

GLPFN void (GLAPIENTRYP pfn_glDeleteTextures)(GLsizei n, const GLuint *textures);
#define glDeleteTextures pfn_glDeleteTextures

GLPFN void (GLAPIENTRYP pfn_glDepthFunc)(GLenum func);
#define glDepthFunc pfn_glDepthFunc

GLPFN void (GLAPIENTRYP pfn_glDepthMask)(GLboolean flag);
#define glDepthMask pfn_glDepthMask

GLPFN void (GLAPIENTRYP pfn_glDepthRange)(GLclampd zNear, GLclampd zFar);
#define glDepthRange pfn_glDepthRange

GLPFN void (GLAPIENTRYP pfn_glDisable)(GLenum cap);
#define glDisable pfn_glDisable

GLPFN void (GLAPIENTRYP pfn_glDisableClientState)(GLenum array);
#define glDisableClientState pfn_glDisableClientState

GLPFN void (GLAPIENTRYP pfn_glDrawArrays)(GLenum mode, GLint first, GLsizei count);
#define glDrawArrays pfn_glDrawArrays

GLPFN void (GLAPIENTRYP pfn_glDrawElements)(GLenum mode, GLsizei count, GLenum type, const void *indices);
#define glDrawElements pfn_glDrawElements

GLPFN void (GLAPIENTRYP pfn_glEnable)(GLenum cap);
#define glEnable pfn_glEnable

GLPFN void (GLAPIENTRYP pfn_glEnableClientState)(GLenum array);
#define glEnableClientState pfn_glEnableClientState

GLPFN void (GLAPIENTRYP pfn_glEnd)(void);
#define glEnd pfn_glEnd

GLPFN void (GLAPIENTRYP pfn_glFinish)(void);
#define glFinish pfn_glFinish

GLPFN void (GLAPIENTRYP pfn_glFlush)(void);
#define glFlush pfn_glFlush

GLPFN void (GLAPIENTRYP pfn_glFogf)(GLenum pname, GLfloat param);
#define glFogf pfn_glFogf

GLPFN void (GLAPIENTRYP pfn_glFogfv)(GLenum pname, const GLfloat *params);
#define glFogfv pfn_glFogfv

GLPFN void (GLAPIENTRYP pfn_glFogi)(GLenum pname, GLint param);
#define glFogi pfn_glFogi

GLPFN void (GLAPIENTRYP pfn_glFrontFace)(GLenum mode);
#define glFrontFace pfn_glFrontFace

GLPFN void (GLAPIENTRYP pfn_glGenTextures)(GLsizei n, GLuint *textures);
#define glGenTextures pfn_glGenTextures

GLPFN void (GLAPIENTRYP pfn_glGetBooleanv)(GLenum pname, GLboolean *params);
#define glGetBooleanv pfn_glGetBooleanv

GLPFN GLenum (GLAPIENTRYP pfn_glGetError)(void);
#define glGetError pfn_glGetError

GLPFN void (GLAPIENTRYP pfn_glGetFloatv)(GLenum pname, GLfloat *params);
#define glGetFloatv pfn_glGetFloatv

GLPFN void (GLAPIENTRYP pfn_glGetIntegerv)(GLenum pname, GLint *params);
#define glGetIntegerv pfn_glGetIntegerv

GLPFN const GLubyte * (GLAPIENTRYP pfn_glGetString)(GLenum name);
#define glGetString pfn_glGetString

GLPFN void (GLAPIENTRYP pfn_glGetTexImage)(GLenum target, GLint level, GLenum format, GLenum type, void *pixels);
#define glGetTexImage pfn_glGetTexImage

GLPFN void (GLAPIENTRYP pfn_glLightModelfv)(GLenum pname, const GLfloat *params);
#define glLightModelfv pfn_glLightModelfv

GLPFN void (GLAPIENTRYP pfn_glLightf)(GLenum light, GLenum pname, GLfloat param);
#define glLightf pfn_glLightf

GLPFN void (GLAPIENTRYP pfn_glLightfv)(GLenum light, GLenum pname, const GLfloat *params);
#define glLightfv pfn_glLightfv

GLPFN void (GLAPIENTRYP pfn_glLineWidth)(GLfloat width);
#define glLineWidth pfn_glLineWidth

GLPFN void (GLAPIENTRYP pfn_glLoadIdentity)(void);
#define glLoadIdentity pfn_glLoadIdentity

GLPFN void (GLAPIENTRYP pfn_glLoadMatrixf)(const GLfloat *m);
#define glLoadMatrixf pfn_glLoadMatrixf

GLPFN void (GLAPIENTRYP pfn_glMaterialfv)(GLenum face, GLenum pname, const GLfloat *params);
#define glMaterialfv pfn_glMaterialfv

GLPFN void (GLAPIENTRYP pfn_glMatrixMode)(GLenum mode);
#define glMatrixMode pfn_glMatrixMode

GLPFN void (GLAPIENTRYP pfn_glMultMatrixf)(const GLfloat *m);
#define glMultMatrixf pfn_glMultMatrixf

GLPFN void (GLAPIENTRYP pfn_glNormalPointer)(GLenum type, GLsizei stride, const void *pointer);
#define glNormalPointer pfn_glNormalPointer

GLPFN void (GLAPIENTRYP pfn_glOrtho)(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble nearVal, GLdouble farVal);
#define glOrtho pfn_glOrtho

GLPFN void (GLAPIENTRYP pfn_glPixelStorei)(GLenum pname, GLint param);
#define glPixelStorei pfn_glPixelStorei

GLPFN void (GLAPIENTRYP pfn_glPointSize)(GLfloat size);
#define glPointSize pfn_glPointSize

GLPFN void (GLAPIENTRYP pfn_glPolygonMode)(GLenum face, GLenum mode);
#define glPolygonMode pfn_glPolygonMode

GLPFN void (GLAPIENTRYP pfn_glPolygonOffset)(GLfloat factor, GLfloat units);
#define glPolygonOffset pfn_glPolygonOffset

GLPFN void (GLAPIENTRYP pfn_glPopAttrib)(void);
#define glPopAttrib pfn_glPopAttrib

GLPFN void (GLAPIENTRYP pfn_glPopMatrix)(void);
#define glPopMatrix pfn_glPopMatrix

GLPFN void (GLAPIENTRYP pfn_glPushAttrib)(GLbitfield mask);
#define glPushAttrib pfn_glPushAttrib

GLPFN void (GLAPIENTRYP pfn_glPushMatrix)(void);
#define glPushMatrix pfn_glPushMatrix

GLPFN void (GLAPIENTRYP pfn_glScissor)(GLint x, GLint y, GLsizei width, GLsizei height);
#define glScissor pfn_glScissor

GLPFN void (GLAPIENTRYP pfn_glShadeModel)(GLenum mode);
#define glShadeModel pfn_glShadeModel

GLPFN void (GLAPIENTRYP pfn_glStencilFunc)(GLenum func, GLint ref, GLuint mask);
#define glStencilFunc pfn_glStencilFunc

GLPFN void (GLAPIENTRYP pfn_glStencilMask)(GLuint mask);
#define glStencilMask pfn_glStencilMask

GLPFN void (GLAPIENTRYP pfn_glStencilOp)(GLenum fail, GLenum zfail, GLenum zpass);
#define glStencilOp pfn_glStencilOp

GLPFN void (GLAPIENTRYP pfn_glTexCoord2f)(GLfloat s, GLfloat t);
#define glTexCoord2f pfn_glTexCoord2f

GLPFN void (GLAPIENTRYP pfn_glTexCoordPointer)(GLint size, GLenum type, GLsizei stride, const void *pointer);
#define glTexCoordPointer pfn_glTexCoordPointer

GLPFN void (GLAPIENTRYP pfn_glTexImage2D)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels);
#define glTexImage2D pfn_glTexImage2D

GLPFN void (GLAPIENTRYP pfn_glTexParameterf)(GLenum target, GLenum pname, GLfloat param);
#define glTexParameterf pfn_glTexParameterf

GLPFN void (GLAPIENTRYP pfn_glTexParameterfv)(GLenum target, GLenum pname, const GLfloat *params);
#define glTexParameterfv pfn_glTexParameterfv

GLPFN void (GLAPIENTRYP pfn_glTexParameteri)(GLenum target, GLenum pname, GLint param);
#define glTexParameteri pfn_glTexParameteri

GLPFN void (GLAPIENTRYP pfn_glTexSubImage2D)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels);
#define glTexSubImage2D pfn_glTexSubImage2D

GLPFN void (GLAPIENTRYP pfn_glVertex2i)(GLint x, GLint y);
#define glVertex2i pfn_glVertex2i

GLPFN void (GLAPIENTRYP pfn_glVertexPointer)(GLint size, GLenum type, GLsizei stride, const void *pointer);
#define glVertexPointer pfn_glVertexPointer

GLPFN void (GLAPIENTRYP pfn_glViewport)(GLint x, GLint y, GLsizei width, GLsizei height);
#define glViewport pfn_glViewport

/*
 * Extension functions (not exported from OpenGL32 on Windows).
 */
GLPFN void (GLAPIENTRYP pfn_glBlendColor)(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
#define glBlendColor pfn_glBlendColor

GLPFN void (GLAPIENTRYP pfn_glBlendEquation)(GLenum mode);
#define glBlendEquation pfn_glBlendEquation

GLPFN void (GLAPIENTRYP pfn_glClientActiveTexture)(GLenum texture);
#define glClientActiveTexture pfn_glClientActiveTexture

#ifdef RT_OS_WINDOWS
/*
 * WGL.
 */
GLPFN HGLRC (WINAPI *pfn_wglCreateContext)(HDC);
#define wglCreateContext pfn_wglCreateContext

GLPFN BOOL (WINAPI *pfn_wglDeleteContext)(HGLRC);
#define wglDeleteContext pfn_wglDeleteContext

GLPFN BOOL (WINAPI *pfn_wglMakeCurrent)(HDC, HGLRC);
#define wglMakeCurrent pfn_wglMakeCurrent

GLPFN BOOL (WINAPI *pfn_wglShareLists)(HGLRC, HGLRC);
#define wglShareLists pfn_wglShareLists

#elif defined(RT_OS_LINUX)
/*
 * GLX
 */
GLPFN int (* pfn_glXGetFBConfigAttrib)(Display * dpy, GLXFBConfig config, int attribute, int * value);
#define glXGetFBConfigAttrib pfn_glXGetFBConfigAttrib

GLPFN XVisualInfo * (* pfn_glXGetVisualFromFBConfig)(Display * dpy, GLXFBConfig config);
#define glXGetVisualFromFBConfig pfn_glXGetVisualFromFBConfig

GLPFN Bool (* pfn_glXQueryVersion)(Display * dpy,  int * major,  int * minor);
#define glXQueryVersion pfn_glXQueryVersion

GLPFN GLXFBConfig * (* pfn_glXChooseFBConfig)(Display * dpy, int screen, const int * attrib_list, int * nelements);
#define glXChooseFBConfig pfn_glXChooseFBConfig

GLPFN XVisualInfo* (* pfn_glXChooseVisual)(Display * dpy,  int screen,  int * attribList);
#define glXChooseVisual pfn_glXChooseVisual

GLPFN GLXContext (* pfn_glXCreateContext)(Display * dpy,  XVisualInfo * vis,  GLXContext shareList,  Bool direct);
#define glXCreateContext pfn_glXCreateContext

GLPFN GLXPixmap (* pfn_glXCreatePixmap)(Display * dpy, GLXFBConfig config, Pixmap pixmap, const int * attrib_list);
#define glXCreatePixmap pfn_glXCreatePixmap

GLPFN Bool (* pfn_glXMakeCurrent)(Display * dpy,  GLXDrawable drawable,  GLXContext ctx);
#define glXMakeCurrent pfn_glXMakeCurrent

GLPFN void (* pfn_glXDestroyContext)(Display * dpy,  GLXContext ctx);
#define glXDestroyContext pfn_glXDestroyContext

GLPFN void (* pfn_glXDestroyPixmap)(Display * dpy, GLXPixmap Pixmap);
#define glXDestroyPixmap pfn_glXDestroyPixmap

/*
 * X11
 */
GLPFN int (* pfn_XConfigureWindow)(Display *display, Window w, unsigned value_mask, XWindowChanges *changes);
#define XConfigureWindow pfn_XConfigureWindow

GLPFN int (* pfn_XCloseDisplay)(Display *display);
#define XCloseDisplay pfn_XCloseDisplay

GLPFN Colormap (* pfn_XCreateColormap)(Display *display, Window w, Visual *visual, int alloc);
#define XCreateColormap pfn_XCreateColormap

GLPFN Pixmap (* pfn_XCreatePixmap)(Display *display, Drawable d, unsigned int width, unsigned int height, unsigned int depth);
#define XCreatePixmap pfn_XCreatePixmap

GLPFN Window (* pfn_XCreateWindow)(Display *display, Window parent, int x, int y, unsigned int width, unsigned int height,
    unsigned int border_width, int depth, unsigned int window_class, Visual *visual, unsigned long valuemask, XSetWindowAttributes *attributes);
#define XCreateWindow pfn_XCreateWindow

GLPFN Window (* pfn_XDefaultRootWindow)(Display *display);
#define XDefaultRootWindow pfn_XDefaultRootWindow

GLPFN int (* pfn_XDestroyWindow)(Display *display, Window w);
#define XDestroyWindow pfn_XDestroyWindow

GLPFN int (* pfn_XFree)(void *data);
#define XFree pfn_XFree

GLPFN int (* pfn_XFreePixmap)(Display *display, Pixmap pixmap);
#define XFreePixmap pfn_XFreePixmap

GLPFN Status (* pfn_XInitThreads)(void);
#define XInitThreads pfn_XInitThreads

GLPFN int (* pfn_XNextEvent)(Display *display, XEvent *event_return);
#define XNextEvent pfn_XNextEvent

GLPFN Display *(* pfn_XOpenDisplay)(char *display_name);
#define XOpenDisplay pfn_XOpenDisplay

GLPFN int (* pfn_XPending)(Display *display);
#define XPending pfn_XPending

GLPFN int (* (* pfn_XSetErrorHandler)(int (*handler)(Display *, XErrorEvent *)))(Display *, XErrorEvent *);
#define XSetErrorHandler pfn_XSetErrorHandler

GLPFN int (* pfn_XSync)(Display *display, Bool discard);
#define XSync pfn_XSync

GLPFN int (* pfn_XScreenNumberOfScreen)(Screen *screen);
#define XScreenNumberOfScreen pfn_XScreenNumberOfScreen

GLPFN int (* pfn_XMapWindow)(Display *display, Window w);
#define XMapWindow pfn_XMapWindow

GLPFN Status (* pfn_XGetWindowAttributes)(Display *display, Window w, XWindowAttributes *window_attributes_return);
#define XGetWindowAttributes pfn_XGetWindowAttributes

#endif

#endif /* !VBOX_INCLUDED_SRC_Graphics_DevVGA_SVGA3d_glLdr_h */
