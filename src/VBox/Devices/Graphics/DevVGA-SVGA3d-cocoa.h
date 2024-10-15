/* $Id: DevVGA-SVGA3d-cocoa.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox OpenGL Cocoa Window System Helper Implementation.
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

#ifndef VBOX_INCLUDED_SRC_Graphics_DevVGA_SVGA3d_cocoa_h
#define VBOX_INCLUDED_SRC_Graphics_DevVGA_SVGA3d_cocoa_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/types.h>
#include <VBox/VBoxCocoa.h>

RT_C_DECLS_BEGIN

#ifndef ___renderspu_cocoa_helper_h
ADD_COCOA_NATIVE_REF(NSView);
ADD_COCOA_NATIVE_REF(NSOpenGLContext);
#endif

#ifdef IN_VMSVGA3DCOCOA
# define VMSVGA3DCOCOA_DECL(type)  DECLEXPORT(type)
#else
# define VMSVGA3DCOCOA_DECL(type)  DECLIMPORT(type)
#endif

VMSVGA3DCOCOA_DECL(void) vmsvga3dCocoaServiceRunLoop(void);
VMSVGA3DCOCOA_DECL(bool) vmsvga3dCocoaCreateViewAndContext(NativeNSViewRef *ppView, NativeNSOpenGLContextRef *ppCtx,
                                                           NativeNSViewRef pParentView, uint32_t cx, uint32_t cy,
                                                           NativeNSOpenGLContextRef pSharedCtx, bool fOtherProfile);
VMSVGA3DCOCOA_DECL(void) vmsvga3dCocoaDestroyViewAndContext(NativeNSViewRef pView, NativeNSOpenGLContextRef pCtx);
VMSVGA3DCOCOA_DECL(void) vmsvga3dCocoaViewInfo(PCDBGFINFOHLP pHlp, NativeNSViewRef pView);
VMSVGA3DCOCOA_DECL(void) vmsvga3dCocoaViewSetPosition(NativeNSViewRef pView, NativeNSViewRef pParentView, int x, int y);
VMSVGA3DCOCOA_DECL(void) vmsvga3dCocoaViewSetSize(NativeNSViewRef pView, int w, int h);
VMSVGA3DCOCOA_DECL(void) vmsvga3dCocoaViewUpdateViewport(NativeNSViewRef pView);
VMSVGA3DCOCOA_DECL(void) vmsvga3dCocoaViewMakeCurrentContext(NativeNSViewRef pView, NativeNSOpenGLContextRef pCtx);
VMSVGA3DCOCOA_DECL(void) vmsvga3dCocoaSwapBuffers(NativeNSViewRef pView, NativeNSOpenGLContextRef pCtx);

int ExplicitlyLoadVBoxSVGA3DObjC(bool fResolveAllImports, PRTERRINFO pErrInfo);

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_Graphics_DevVGA_SVGA3d_cocoa_h */

