/* $Id: utils.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevVMWare/Shaderlib - Utility/Stub Functions & Data.
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

#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/assert.h>
#ifdef _MSC_VER
# include <iprt/win/windows.h>
#else
# include <windows.h>
#endif
#include "wined3d_private.h"



void *wined3d_rb_alloc(size_t size)
{
    return RTMemAlloc(size);
}

void *wined3d_rb_realloc(void *ptr, size_t size)
{
    return RTMemRealloc(ptr, size);
}

void wined3d_rb_free(void *ptr)
{
    RTMemFree(ptr);
}

/* This small helper function is used to convert a bitmask into the number of masked bits */
unsigned int count_bits(unsigned int mask)
{
    unsigned int count;
    for (count = 0; mask; ++count)
    {
        mask &= mask - 1;
    }
    return count;
}

UINT wined3d_log2i(UINT32 x)
{
    static const UINT l[] =
    {
        ~0U, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
          4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
          5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
          5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
          6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
          6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
          6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
          6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
          7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
          7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
          7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
          7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
          7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
          7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
          7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
          7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    };
    UINT32 i;

    return (i = x >> 16) ? (x = i >> 8) ? l[x] + 24 : l[i] + 16 : (i = x >> 8) ? l[i] + 8 : l[x];
}

/* Set the shader type for this device, depending on the given capabilities
 * and the user preferences in wined3d_settings. */
void select_shader_mode(const struct wined3d_gl_info *gl_info, int *ps_selected, int *vs_selected)
{
    RT_NOREF(gl_info);
    *vs_selected = SHADER_GLSL;
    *ps_selected = SHADER_GLSL;
}

const char *debug_glerror(GLenum error) {
    switch(error) {
#define GLERROR_TO_STR(u) case u: return #u
        GLERROR_TO_STR(GL_NO_ERROR);
        GLERROR_TO_STR(GL_INVALID_ENUM);
        GLERROR_TO_STR(GL_INVALID_VALUE);
        GLERROR_TO_STR(GL_INVALID_OPERATION);
        GLERROR_TO_STR(GL_STACK_OVERFLOW);
        GLERROR_TO_STR(GL_STACK_UNDERFLOW);
        GLERROR_TO_STR(GL_OUT_OF_MEMORY);
        GLERROR_TO_STR(GL_INVALID_FRAMEBUFFER_OPERATION);
#undef GLERROR_TO_STR
        default:
            return "unrecognized";
    }
}

void dump_color_fixup_desc(struct color_fixup_desc fixup)
{
    RT_NOREF(fixup);
}

void context_release(struct wined3d_context *context)
{
    RT_NOREF(context);
}

static void CDECL wined3d_do_nothing(void)
{
}

void (* CDECL wine_tsx11_lock_ptr)(void)   = wined3d_do_nothing;
void (* CDECL wine_tsx11_unlock_ptr)(void) = wined3d_do_nothing;

LPVOID      WINAPI VBoxHeapAlloc(HANDLE hHeap, DWORD heaptype,SIZE_T size)
{
    RT_NOREF(hHeap, heaptype);
    return RTMemAllocZ(size);
}

BOOL        WINAPI VBoxHeapFree(HANDLE hHeap, DWORD heaptype,LPVOID ptr)
{
    RT_NOREF(hHeap, heaptype);
    RTMemFree(ptr);
    return TRUE;
}

LPVOID      WINAPI VBoxHeapReAlloc(HANDLE hHeap, DWORD heaptype, LPVOID ptr, SIZE_T size)
{
    RT_NOREF(hHeap, heaptype);
    return RTMemRealloc(ptr, size);
}

void VBoxDebugBreak(void)
{
    AssertFailed();
}

