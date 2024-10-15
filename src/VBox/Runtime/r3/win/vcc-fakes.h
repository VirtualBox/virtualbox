/* $Id: vcc-fakes.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Common macros for the Visual C++ 2010+ CRT import fakes.
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

#ifndef IPRT_INCLUDED_SRC_r3_win_vcc_fakes_h
#define IPRT_INCLUDED_SRC_r3_win_vcc_fakes_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifdef RT_STRICT
# include <stdio.h> /* _snprintf */
#endif


/** @def MY_ASSERT
 * We use a special assertion macro here to avoid dragging in IPRT bits in
 * places which cannot handle it (direct GA 3D bits or something like that).
 *
 * Turns out snprintf is off limits too.  Needs Locale info and runs out of
 * stack
 */
#ifdef RT_STRICT
# if 1
#  define MY_ASSERT(a_Expr, g_szMsg) \
    do { \
        if ((a_Expr)) \
        { /*likely*/ } \
        else \
        { \
            OutputDebugStringA("Assertion failed on line " RT_XSTR(__LINE__) ": " RT_XSTR(a_Expr) "\n"); \
            OutputDebugStringA("Assertion message: " g_szMsg "\n"); \
            RT_BREAKPOINT(); \
        } \
    } while (0)
# define MY_ASSERT_STMT_RETURN(a_Expr, a_Stmt, a_rc) \
    do { \
        if (a_Expr) \
        { /* likely */ } \
        else \
        { \
            OutputDebugStringA("Assertion failed on line " RT_XSTR(__LINE__) ": " RT_XSTR(a_Expr) "\n"); \
            RT_BREAKPOINT(); \
            a_Stmt; \
            return (a_rc); \
        } \
    } while (0)
# else
#  define MY_ASSERT(a_Expr, ...) \
    do { \
        if ((a_Expr)) \
        { /*likely*/ } \
        else \
        { \
            char szTmp[256]; \
            _snprintf(szTmp, sizeof(szTmp), "Assertion failed on line %u in '%s': %s", __LINE__, __PRETTY_FUNCTION__, #a_Expr); \
            OutputDebugStringA(szTmp); \
            _snprintf(szTmp, sizeof(szTmp), __VA_ARGS__); \
            OutputDebugStringA(szTmp); \
            RT_BREAKPOINT(); \
        } \
    } while (0)
# endif
#else
# define MY_ASSERT(a_Expr, ...) do { } while (0)
# define MY_ASSERT_STMT_RETURN(a_Expr, a_Stmt, a_rc) \
    do { if (a_Expr) { /* likely */ } else { a_Stmt; return (a_rc); }} while (0)
#endif


/** Dynamically resolves an NTDLL API we need.   */
#define RESOLVE_NTDLL_API(ApiNm) \
    static bool volatile    s_fInitialized##ApiNm = false; \
    static decltype(ApiNm) *s_pfn##ApiNm = NULL; \
    decltype(ApiNm)        *pfn##ApiNm; \
    if (s_fInitialized##ApiNm) \
        pfn##ApiNm = s_pfn##ApiNm; \
    else \
    { \
        pfn##ApiNm = (decltype(pfn##ApiNm))GetProcAddress(GetModuleHandleW(L"ntdll"), #ApiNm); \
        s_pfn##ApiNm = pfn##ApiNm; \
        s_fInitialized##ApiNm = true; \
    } do {} while (0)


/** Declare a kernel32 API.
 * @note We are not exporting them as that causes duplicate symbol troubles in
 *       the OpenGL bits. */
#define DECL_KERNEL32(a_Type) extern "C" a_Type WINAPI


/** Ignore comments. */
#define COMMENT(a_Text)

/** Used for MAKE_IMPORT_ENTRY when declaring external g_pfnXxxx variables. */
#define DECLARE_FUNCTION_POINTER(a_Name, a_cb) extern "C" decltype(a_Name) *RT_CONCAT(g_pfn, a_Name);

/** Used in the InitFakes method to decl are uCurVersion used by assertion. */
#ifdef RT_STRICT
# define CURRENT_VERSION_VARIABLE() \
    unsigned uCurVersion = GetVersion(); \
    uCurVersion = ((uCurVersion & 0xff) << 8) | ((uCurVersion >> 8) & 0xff)
#else
# define CURRENT_VERSION_VARIABLE() (void)0
#endif


/** Used for MAKE_IMPORT_ENTRY when resolving an import. */
#define RESOLVE_IMPORT(a_uMajorVer, a_uMinorVer, a_Name, a_cb) \
    do { \
        FARPROC pfnApi = GetProcAddress(hmod, #a_Name); \
        if (pfnApi) \
            RT_CONCAT(g_pfn, a_Name) = (decltype(a_Name) *)pfnApi; \
        else \
        { \
            MY_ASSERT(uCurVersion < (((a_uMajorVer) << 8) | (a_uMinorVer)), #a_Name); \
            RT_CONCAT(g_pfn, a_Name) = RT_CONCAT(Fake_,a_Name); \
        } \
    } while (0);


#endif /* !IPRT_INCLUDED_SRC_r3_win_vcc_fakes_h */

