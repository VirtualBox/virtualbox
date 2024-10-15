/* $Id: vcc-fakes-shell32.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Tricks to make the Visual C++ 2010 CRT work on NT4, W2K and XP.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define RT_NO_STRICT /* Minimal deps so that it works on NT 3.51 too. */
#include <iprt/types.h>
#include <iprt/assert.h>
#include <iprt/string.h>

#ifndef RT_ARCH_X86
# error "This code is X86 only"
#endif

#define CommandLineToArgvW                      Ignore_CommandLineToArgvW

#include <iprt/nt/nt-and-windows.h>

#undef CommandLineToArgvW


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Dynamically resolves an kernel32 API.   */
#define RESOLVE_ME(ApiNm) \
    static decltype(ShellExecuteW) * volatile s_pfnInitialized = NULL; \
    static decltype(ApiNm) *s_pfnApi = NULL; \
    decltype(ApiNm)        *pfnApi; \
    if (s_pfnInitialized) \
        pfnApi = s_pfnApi; \
    else \
    { \
        pfnApi = (decltype(pfnApi))GetProcAddress(GetModuleHandleW(L"shell32"), #ApiNm); \
        s_pfnApi = pfnApi; \
        s_pfnInitialized = ShellExecuteW; /* ensures shell32 is loaded */ \
    } do {} while (0)


/** Declare a shell32 API.
 * @note We are not exporting them as that causes duplicate symbol troubles in
 *       the OpenGL bits. */
#define DECL_SHELL32(a_Type) extern "C" a_Type WINAPI


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


DECL_SHELL32(LPWSTR *) CommandLineToArgvW(LPCWSTR pwszCmdLine, int *pcArgs)
{
    RESOLVE_ME(CommandLineToArgvW);
    if (pfnApi)
        return pfnApi(pwszCmdLine, pcArgs);

    *pcArgs = 0;
    return NULL;
}


/* Dummy to force dragging in this object in the link, so the linker
   won't accidentally use the symbols from shell32. */
extern "C" int vcc100_shell32_fakes_cpp(void)
{
    return 42;
}

