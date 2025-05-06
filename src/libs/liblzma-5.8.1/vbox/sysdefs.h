/* $Id: sysdefs.h 109441 2025-05-06 17:40:59Z samantha.scholz@oracle.com $ */
/** @file
 * sysdefs.h - System definitions using IPRT.
 */

/*
 * Copyright (C) 2023 Oracle and/or its affiliates.
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

#ifndef LZMA_SYSDEFS_H
#define LZMA_SYSDEFS_H

//////////////
// Includes //
//////////////

#include <iprt/nocrt/limits.h>

#include <iprt/cdefs.h>
#include <iprt/stdint.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/mp.h>
#include <iprt/system.h>

#define TUKLIB_DECLS_BEGIN RT_C_DECLS_BEGIN
#define TUKLIB_DECLS_END   RT_C_DECLS_END

// TUKLIB_SYMBOL_PREFIX is prefixed to all symbols exported by
// the tuklib modules. If you use a tuklib module in a library,
// you should use TUKLIB_SYMBOL_PREFIX to make sure that there
// are no symbol conflicts in case someone links your library
// into application that also uses the same tuklib module.
#ifndef TUKLIB_SYMBOL_PREFIX
#	define TUKLIB_SYMBOL_PREFIX
#endif

#ifndef TUKLIB_SYMBOL
# define TUKLIB_SYMBOL(a_Sym) RT_CONCAT(TUKLIB_SYMBOL_PREFIX, a_Sym)
#endif

#define TUKLIB_GNUC_REQ(major, minor) RT_GNUC_PREREQ(major, minor)
#define assert(expr) Assert(expr)

#if 0
// The code currently assumes that size_t is either 32-bit or 64-bit.
#ifndef SIZE_MAX
#	if SIZEOF_SIZE_T == 4
#		define SIZE_MAX UINT32_MAX
#	elif SIZEOF_SIZE_T == 8
#		define SIZE_MAX UINT64_MAX
#	else
#		error size_t is not 32-bit or 64-bit
#	endif
#endif
#if SIZE_MAX != UINT32_MAX && SIZE_MAX != UINT64_MAX
#	error size_t is not 32-bit or 64-bit
#endif

#include <stdlib.h>
#include <assert.h>

// Pre-C99 systems lack stdbool.h. All the code in XZ Utils must be written
// so that it works with fake bool type, for example:
//
//    bool foo = (flags & 0x100) != 0;
//    bool bar = !!(flags & 0x100);
//
// This works with the real C99 bool but breaks with fake bool:
//
//    bool baz = (flags & 0x100);
//
#ifdef HAVE_STDBOOL_H
#	include <stdbool.h>
#else
#	if ! HAVE__BOOL
typedef unsigned char _Bool;
#	endif
#	define bool _Bool
#	define false 0
#	define true 1
#	define __bool_true_false_are_defined 1
#endif

#include <string.h>

// As of MSVC 2013, inline and restrict are supported with
// non-standard keywords.
#if defined(_WIN32) && defined(_MSC_VER)
#	ifndef inline
#		define inline __inline
#	endif
#	ifndef restrict
#		define restrict __restrict
#	endif
#endif

#endif

// We may need alignas from C11/C17/C23.
#if __STDC_VERSION__ >= 202311
	// alignas is a keyword in C23. Do nothing.
#elif __STDC_VERSION__ >= 201112
	// Oracle Developer Studio 12.6 lacks <stdalign.h>.
	// For simplicity, avoid the header with all C11/C17 compilers.
#	define alignas _Alignas
#elif defined(__GNUC__) || defined(__clang__)
#	define alignas(n) __attribute__((__aligned__(n)))
#else
#	define alignas(n)
#endif

// MSVC v19.00 (VS 2015 version 14.0) and later should work.
//
// MSVC v19.27 (VS 2019 version 16.7) added support for restrict.
// Older ones support only __restrict.
#ifdef _MSC_VER
#	if _MSC_VER < 1927 && !defined(restrict)
#		define restrict __restrict
#	endif
#endif

////////////
// Macros //
////////////

#undef memzero
#define memzero(s, n) memset(s, 0, n)
#define my_min(x, y) RT_MIN(x, y)
#define my_max(x, y) RT_MAX(x, y)

#ifndef ARRAY_SIZE
#	define ARRAY_SIZE(array) RT_ELEMENTS(array)
#endif

#if defined(__GNUC__) \
		&& ((__GNUC__ == 4 && __GNUC_MINOR__ >= 3) || __GNUC__ > 4)
#	define lzma_attr_alloc_size(x) __attribute__((__alloc_size__(x)))
#else
#	define lzma_attr_alloc_size(x)
#endif

#if __STDC_VERSION__ >= 202311
#	define FALLTHROUGH [[__fallthrough__]]
#elif (defined(__GNUC__) && __GNUC__ >= 7) \
		|| (defined(__clang_major__) && __clang_major__ >= 10)
#	define FALLTHROUGH __attribute__((__fallthrough__))
#else
#	define FALLTHROUGH ((void)0)
#endif

#endif
